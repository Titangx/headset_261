'''
Copyright (c) 2020 Qualcomm Technologies International, Ltd.

A script to generate the c code for a rules table from an input description of a rules table.

Note: A *.rules file can contain one or more separate rules tables but at the moment only the first
table in the list is output.

Description of rules table format
---------------------------------


'''

from __future__ import print_function

import sys
import os
import json
try:
    import yaml
except ImportError:
    yaml = None

file_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(file_dir)
sys.path.insert(0, os.path.join(parent_dir, "codegen"))
from c_codegen import CommentDoxy, CommentBlock, CommentBlockDoxygen, HeaderGuards, Array, Include, Indented


class RulesGenerator(object):
    ''' Rules generator class '''
    def __init__(self, stream):
        self._rules_format = 'n/a'
        self._rules_tables = []
        self._rules_name = ''
        self._rules_includes = []

        self._parse_input_file(stream)
        self._process_rules_table_includes(os.path.dirname(os.path.realpath(stream.name)))

    def _parse_input_file(self, stream):
        ''' This needs to be implemented by a child class based on the file format it supports

            The function needs to read in the following data:
            _rules_tables A list of one or more rules table dictionaries
            _rules_name : The name of the first rules table
            _rules_includes : A list of the C includes for the first rules table

            Note: currently only the first rules table is used when generating the output.

            Each entry in the _rules_tables contains:
            {
                name : Name of the rules table
                doc : Doxygen comment for the table
                include_headers : A list of strings of the C includes
                rules : A list of individual rules
                {
                    event_mask : Event(s) that cause a rule to be evaluated
                    rule_function : The C function that is called to decide if a rule will run
                    output_message_id : MessageId generated when the rule is run
                    doc: Doxygen comment for this rule
                }
                rules_includes : A list of external rules tables files to take rules from and append to this rules table.
                {
                    file : Filename of external *.rules file
                    rules_table_name : The name of the rules_table to take the rules from
                }
            }
        '''
        raise NotImplementedError()

    def _process_rules_table_includes(self, include_path):
        ''' Process any rules_includes elements in a rules_table '''
        for t in self._rules_tables:
            if t.get('rules_includes'):
                for i in t.get('rules_includes'):
                    # Import the external rules table
                    filepath = os.path.abspath(os.path.join(include_path, i.get('file')))
                    with open(filepath, 'r') as external_file:
                        external_rules = CreateRulesFile(self._rules_format, external_file)
                        extra_rules = []
                        # Append the external rules to the end of this rules table
                        for et in external_rules._rules_tables:
                            if et.get('name') == i.get('rules_table_name'):
                                extra_rules = et.get('rules')
                                break

                        if extra_rules:
                            t.setdefault('rules', []).extend(extra_rules)
                        else:
                            raise ValueError('Could not find a rules table named "{}" in {}'.format(i.get('rules_table_name'), i.get('file')))


    def _generate_dox_header(self, file_type=""):
        ''' Generate doxygen file header comment '''
        with CommentBlockDoxygen() as cbd:
            cbd.doxy_copyright()
            cbd.doxy_version()
            cbd.doxy_filename("")
            cbd.doxy_brief("The {} {}. This file is generated by {}.".format(self._rules_name, file_type, os.path.basename(__file__)))

    def _generate_rules_table_array(self):
        ''' Generate table of rule entries '''
        for t in self._rules_tables:
            print("const size_t {}_rules_count = {};\n".format(t.get('name'), len(t.get('rules'))))

            with Array("const rule_entry_t", "{}_rules".format(t.get('name')), t.get('doc'), eol=',\n') as arr:
                for r in t.get('rules'):
                    arr.extend_with_doc([self._generate_rule_entry(r)], [r.get('doc')])

    def _generate_rules_table_array_declarations(self):
        ''' Generate table of rule declarations '''
        for t in self._rules_tables:
            if "doc" in t:
                CommentDoxy().add(t.get('doc'))

            print("extern const size_t {}_rules_count;".format(t.get('name')))
            print("extern const rule_entry_t {}_rules[];".format(t.get('name')))
            print("\n")

    def _generate_rule_entry(self, r):
        ''' Return the rule entry macro line '''
        decl = "RULE({}, {}, {})".format(r.get('event_mask'), r.get('rule_function'), r.get('output_message_id'))

        return decl

    def _print_headers(self, extra=None):
        ''' Add headers required in all files then add xml defined and extras '''
        headers = ["rules_engine.h"]
        if self._rules_includes:
            headers.extend(self._rules_includes)
        if extra:
            headers.extend(extra)

        Include(headers)

    def generate_rules_table_header(self):
        ''' Generate a header file declaring the rules tables in the root '''
        self._generate_dox_header("rule table declarations")
        with HeaderGuards(self._rules_name + "_RULES_TABLE"):
            self._print_headers()
            self._generate_rules_table_array_declarations()

    def generate_rules_table_source(self):
        ''' Generate a source file defining the rules table in the root '''
        self._generate_dox_header("rule table definitions")
        self._print_headers()
        self._generate_rules_table_array()

class YamlRulesGenerator(RulesGenerator):
    ''' Rules generator for YAML format input files '''

    def _parse_input_file(self, stream):
        ''' Open a YAML format file and parse the rules tables it contains '''
        self._rules_format = 'yaml'
        self._rules_yaml = yaml.safe_load(stream)
        self._rules_tables = self._rules_yaml.get('rules_tables')
        self._rules_name = self._rules_tables[0].get('name')
        self._rules_includes = self._rules_tables[0].get('include_headers')

class JsonRulesGenerator(RulesGenerator):
    ''' Rules generator for JSON format input files '''

    def _parse_input_file(self, stream):
        ''' Open a YAML format file and parse the rules tables it contains '''
        self._rules_format = 'json'
        self._rules_json = json.load(stream)
        self._rules_tables = self._rules_json.get('rules_tables')
        self._rules_name = self._rules_tables[0].get('name')
        self._rules_includes = self._rules_tables[0].get('include_headers')

def CreateRulesFile(rules_format, stream):
    try:
        if rules_format == 'json':
            rules = JsonRulesGenerator(stream)
        elif rules_format == 'yaml':
            rules = YamlRulesGenerator(stream)
        else:
            raise ValueError('Unsupported rules format {rules_format}s')
    except Exception as e:
        print("Error loading {}".format(stream.name))
        raise e

    return rules

def main():

    import argparse
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('rules_file', type=argparse.FileType('r'),
                        help='The name of the file containing the description of the rule table')
    parser.add_argument('-f', '--format', choices=['json', 'yaml'], default='json', help='The format of the input file. Default is %(default)s')
    parser.add_argument('--rule_table_header', action='store_true', help='Generate a C header file for the rule table')
    parser.add_argument('--rule_table_source', action='store_true', help='Generate a C source file for the rule table')
    args = parser.parse_args()

    if not args.rule_table_header and not args.rule_table_source:
        parser.error("Must set one of --rule_table_header or --rule_table_source")

    if args.format == 'yaml':
        if yaml is None:
            raise ModuleNotFoundError("yaml input format not supported - unable to import pyyaml package")

    rg = CreateRulesFile(args.format, args.rules_file)

    # Generate the rules table header
    if args.rule_table_header:
        rg.generate_rules_table_header()

    # Generate the rules table source
    if args.rule_table_source:
        rg.generate_rules_table_source()

    return True

if __name__ == "__main__":
    if main() is True:
        exit(0)
    else:
        exit(1)
