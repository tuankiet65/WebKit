# Copyright (C) 2011-2023 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Checks WebKit style for JSON files."""

import json
import os
import re
from collections import defaultdict


class JSONChecker(object):
    """Processes JSON lines for checking style."""

    categories = set(('json/syntax',))

    def __init__(self, file_path, handle_style_error):
        self._file_path = file_path
        self._handle_style_error = handle_style_error
        self._handle_style_error.turn_off_line_filtering()

    def check(self, lines):
        try:
            json.loads('\n'.join(lines) + '\n')
        except ValueError as e:
            self._handle_style_error(self.line_number_from_json_exception(e), 'json/syntax', 5, str(e))

    @staticmethod
    def line_number_from_json_exception(error):
        match = re.search(r': line (?P<line>\d+) column \d+', str(error))
        if not match:
            return 0
        return int(match.group('line'))


class JSONContributorsChecker(JSONChecker):
    """Processes contributors.json lines"""

    def check(self, lines):
        super(JSONContributorsChecker, self).check(lines)
        self._handle_style_error(0, 'json/syntax', 5, 'contributors.json should not be modified through the commit queue')


class JSONFeaturesChecker(JSONChecker):
    """Processes the features.json lines"""

    def check(self, lines):
        super(JSONFeaturesChecker, self).check(lines)

        try:
            features_definition = json.loads('\n'.join(lines) + '\n')
            if 'features' not in features_definition:
                self._handle_style_error(0, 'json/syntax', 5, '"features" key not found, the key is mandatory.')
                return

            specification_name_set = set()
            if 'specification' in features_definition:
                previous_specification_name = ''
                for specification_object in features_definition['specification']:
                    if 'name' not in specification_object or not specification_object['name']:
                        self._handle_style_error(0, 'json/syntax', 5, 'The "name" field is mandatory for specifications.')
                        continue
                    name = specification_object['name']

                    if name < previous_specification_name:
                        self._handle_style_error(0, 'json/syntax', 5, 'The specifications should be sorted alphabetically by name, "%s" appears after "%s".' % (name, previous_specification_name))
                    previous_specification_name = name

                    specification_name_set.add(name)
                    if 'url' not in specification_object or not specification_object['url']:
                        self._handle_style_error(0, 'json/syntax', 5, 'The specifciation "%s" does not have an URL' % name)
                        continue

            features_list = features_definition['features']
            previous_feature_name = ''
            for i in range(len(features_list)):
                feature = features_list[i]
                feature_name = 'Feature %s' % i
                if 'name' not in feature or not feature['name']:
                    self._handle_style_error(0, 'json/syntax', 5, 'The feature %d does not have the mandatory field "name".' % i)
                else:
                    feature_name = feature['name']

                    if feature_name < previous_feature_name:
                        self._handle_style_error(0, 'json/syntax', 5, 'The features should be sorted alphabetically by name, "%s" appears after "%s".' % (feature_name, previous_feature_name))
                    previous_feature_name = feature_name

                if 'status' not in feature or not feature['status']:
                    self._handle_style_error(0, 'json/syntax', 5, 'The feature "%s" does not have the mandatory field "status".' % feature_name)
                if 'specification' in feature:
                    if feature['specification'] not in specification_name_set:
                        self._handle_style_error(0, 'json/syntax', 5, 'The feature "%s" has a specification field but no specification of that name exists.' % feature_name)
        except Exception as e:
            print(e)
            pass


class JSONCSSPropertiesChecker(JSONChecker):
    """Processes CSSProperties.json"""

    def check(self, lines):
        super(JSONCSSPropertiesChecker, self).check(lines)

        try:
            properties_definition = json.loads('\n'.join(lines) + '\n')

            if 'categories' not in properties_definition:
                self._handle_style_error(0, 'json/syntax', 5, '"categories" key not found, the key is mandatory.')
                return
            self._categories = properties_definition['categories']
            self.check_categories()

            if 'shared-grammar-rules' not in properties_definition:
                self._handle_style_error(0, 'json/syntax', 5, '"shared-grammar-rules" key not found, the key is mandatory.')
                return
            self._shared_grammar_rules = properties_definition['shared-grammar-rules']
            self.check_shared_grammar_rules()

            if 'descriptors' not in properties_definition:
                self._handle_style_error(0, 'json/syntax', 5, '"descriptors" key not found, the key is mandatory.')
                return
            self._descriptors = properties_definition['descriptors']
            self.check_descriptors()

            if 'properties' not in properties_definition:
                self._handle_style_error(0, 'json/syntax', 5, '"properties" key not found, the key is mandatory.')
                return
            self._properties = properties_definition['properties']
            self.check_properties()

        except Exception as e:
            print(e)
            pass

    def check_category(self, category_name, category_value):
        keys_and_validators = {
            'shortname': self.validate_string,
            'longname': self.validate_string,
            'url': self.validate_url,
            'status': self.validate_status,
        }
        for key, value in category_value.items():
            if key not in keys_and_validators:
                self._handle_style_error(0, 'json/syntax', 5, 'dictionary for category "%s" has unexpected key "%s".' % (category_name, key))
                return

            keys_and_validators[key](category_name, "", key, value)

    def check_categories(self):
        if not isinstance(self._categories, dict):
            self._handle_style_error(0, 'json/syntax', 5, '"categories" is not a dictionary.')
            return

        for key, value in self._categories.items():
            self.check_category(key, value)

    def check_shared_grammar_rule(self, rule_name, rule_value):
        keys_and_validators = {
            'aliased-to': self.validate_string,
            'comment': self.validate_comment,
            'exported': self.validate_boolean,
            'grammar': self.validate_string,
            'grammar-comment': self.validate_comment,
            'grammar-function': self.validate_string,
            'grammar-unused': self.validate_string,
            'grammar-unused-reason': self.validate_string,
            'specification': self.validate_specification,
            'status': self.validate_status,
        }
        for key, value in rule_value.items():
            if key not in keys_and_validators:
                self._handle_style_error(0, 'json/syntax', 5, 'dictionary for shared property rule "%s" has unexpected key "%s".' % (rule_name, key))
                return

            keys_and_validators[key](rule_name, "", key, value)

    def check_descriptor(self, descriptor_name, descriptor_value):
        keys_and_validators = {
            'values': self.validate_array,
            'codegen-properties': self.validate_codegen_properties,
            'status': self.validate_status,
            'specification': self.validate_specification,
        }

        for key, value in descriptor_value.items():
            if key not in keys_and_validators:
                self._handle_style_error(0, 'json/syntax', 5, 'dictionary for descriptor "%s" has unexpected key "%s".' % (property_name, key))
                return

            keys_and_validators[key](descriptor_name, "", key, value)

    def check_descriptor_kind(self, descriptor_kind, descriptors):
        if descriptor_kind[0] != "@":
            self._handle_style_error(0, 'json/syntax', 5, '"descriptors" key "%s" does not begin with an "@".' % (descriptor_kind))
            return

        if not isinstance(descriptors, dict):
            self._handle_style_error(0, 'json/syntax', 5, '"descriptors.%s" is not a dictionary.' % (descriptor_kind))
            return

        for descriptor_name, descriptor_value in descriptors.items():
            self.check_descriptor(descriptor_name, descriptor_value)

    def check_descriptors(self):
        if not isinstance(self._descriptors, dict):
            self._handle_style_error(0, 'json/syntax', 5, '"descriptors" is not a dictionary.')
            return

        for descriptor_kind, descriptors in self._descriptors.items():
            self.check_descriptor_kind(descriptor_kind, descriptors)

    def check_shared_grammar_rules(self):
        if not isinstance(self._shared_grammar_rules, dict):
            self._handle_style_error(0, 'json/syntax', 5, '"shared-grammar-rules" is not a dictionary.')
            return

        for rule_name, rule_value in self._shared_grammar_rules.items():
            self.check_shared_grammar_rule(rule_name, rule_value)

    def check_properties(self):
        if not isinstance(self._properties, dict):
            self._handle_style_error(0, 'json/syntax', 5, '"properties" is not a dictionary.')
            return

        for property_name, property_value in self._properties.items():
            self.check_property(property_name, property_value)

    def validate_type(self, property_name, property_key, key, value, expected_type):
        if not isinstance(value, expected_type):
            self._handle_style_error(0, 'json/syntax', 5, '"%s" in "%s" %s is not of %s.' % (key, property_name, property_key, expected_type))

    def validate_boolean(self, property_name, property_key, key, value):
        self.validate_type(property_name, property_key, key, value, bool)

    def validate_string(self, property_name, property_key, key, value):
        self.validate_type(property_name, property_key, key, value, str)

    def validate_array(self, property_name, property_key, key, value):
        self.validate_type(property_name, property_key, key, value, list)

    def validate_url(self, property_name, property_key, key, value):
        self.validate_string(property_name, property_key, key, value)
        # FIXME: make sure it's url-like.

    def validate_status_type(self, property_name, property_key, key, value):
        self.validate_string(property_name, property_key, key, value)

        allowed_statuses = {
            'supported',
            'in development',
            'under consideration',
            'experimental',
            'non-standard',
            'not implemented',
            'not considering',
            'obsolete',
            'removed',
        }
        if value not in allowed_statuses:
            self._handle_style_error(0, 'json/syntax', 5, 'status "%s" for property "%s" is not one of the recognized status values' % (value, property_name))

    def validate_comment(self, property_name, property_key, key, value):
        self.validate_string(property_name, property_key, key, value)

    def validate_codegen_properties(self, property_name, property_key, key, value):
        if isinstance(value, list):
            for entry in value:
                self.check_codegen_properties(property_name, entry)
        else:
            self.check_codegen_properties(property_name, value)

    def validate_logical_property_group(self, property_name, property_key, key, value):
        self.validate_type(property_name, property_key, key, value, dict)

        for subKey, value in value.items():
            if subKey in ('name', 'resolver'):
                self.validate_string(property_name, key, subKey, value)
            else:
                self._handle_style_error(0, 'json/syntax', 5, 'dictionary for "%s" of property "%s" has unexpected key "%s".' % (key, property_name, subKey))
                return

    def validate_status(self, property_name, property_key, key, value):
        if isinstance(value, dict):
            keys_and_validators = {
                'comment': self.validate_comment,
                'enabled-by-default': self.validate_boolean,
                'status': self.validate_status_type,
            }

            for key, value in value.items():
                if key not in keys_and_validators:
                    self._handle_style_error(0, 'json/syntax', 5, 'dictionary for "status" of property "%s" has unexpected key "%s".' % (property_name, key))
                    return

                keys_and_validators[key](property_name, "", key, value)
        else:
            self.validate_status_type(property_name, property_key, key, value)

    def validate_property_category(self, property_name, property_key, key, value):
        self.validate_string(property_name, property_key, key, value)

        if value not in self._categories:
            self._handle_style_error(0, 'json/syntax', 5, 'property "%s" has category "%s" which is not in the set of categories.' % (property_name, value))
            return

    def validate_specification(self, property_name, property_key, key, value):
        self.validate_type(property_name, property_key, key, value, dict)

        keys_and_validators = {
            'category': self.validate_property_category,
            'url': self.validate_url,
            'obsolete-category': self.validate_property_category,
            'obsolete-url': self.validate_url,
            'documentation-url': self.validate_url,
            'keywords': self.validate_array,
            'description': self.validate_string,
            'comment': self.validate_comment,
            'non-canonical-url': self.validate_url,
        }

        for key, value in value.items():
            if key not in keys_and_validators:
                self._handle_style_error(0, 'json/syntax', 5, 'dictionary for "specification" of property "%s" has unexpected key "%s".' % (property_name, key))
                return

            keys_and_validators[key](property_name, "specification", key, value)

            # redundant urls

    def check_property(self, property_name, value):
        keys_and_validators = {
            'animation-type': self.validate_string,
            'animation-type-comment': self.validate_comment,
            'codegen-properties': self.validate_codegen_properties,
            'comment': self.validate_comment,
            'inherited': self.validate_boolean,
            'initial': self.validate_string,
            'initial-comment': self.validate_comment,
            'specification': self.validate_specification,
            'status': self.validate_status,
            'values': self.validate_array,
        }

        for key, value in value.items():
            if key not in keys_and_validators:
                self._handle_style_error(0, 'json/syntax', 5, 'dictionary for property "%s" has unexpected key "%s".' % (property_name, key))
                return

            keys_and_validators[key](property_name, "", key, value)

    def check_codegen_properties(self, property_name, codegen_properties):
        if not isinstance(codegen_properties, (dict, list)):
            self._handle_style_error(0, 'json/syntax', 5, '"codegen-properties" for property "%s" is not a dictionary or array.' % property_name)
            return

        keys_and_validators = {
            'accepts-quirky-angle': self.validate_boolean,
            'accepts-quirky-color': self.validate_boolean,
            'accepts-quirky-length': self.validate_boolean,
            'aliases': self.validate_array,
            'animation-getter': self.validate_string,
            'animation-initial': self.validate_string,
            'animation-name-for-methods': self.validate_string,
            'animation-property': self.validate_boolean,
            'animation-setter': self.validate_string,
            'animation-wrapper': self.validate_string,
            'animation-wrapper-acceleration': self.validate_string,
            'animation-wrapper-comment': self.validate_comment,
            'animation-wrapper-requires-additional-parameters': self.validate_array,
            'animation-wrapper-requires-computed-getter': self.validate_boolean,
            'animation-wrapper-requires-non-additive-or-cumulative-interpolation': self.validate_boolean,
            'animation-wrapper-requires-non-normalized-discrete-interpolation': self.validate_boolean,
            'animation-wrapper-requires-override-parameters': self.validate_array,
            'animation-wrapper-requires-render-style': self.validate_boolean,
            'auto-functions': self.validate_boolean,
            'cascade-alias': self.validate_string,
            'color-property': self.validate_boolean,
            'comment': self.validate_string,
            'disables-native-appearance': self.validate_boolean,
            'enable-if': self.validate_string,
            'fast-path-inherited': self.validate_boolean,
            'fill-layer-getter': self.validate_string,
            'fill-layer-initial': self.validate_string,
            'fill-layer-name-for-methods': self.validate_string,
            'fill-layer-property': self.validate_boolean,
            'fill-layer-setter': self.validate_string,
            'font-description-getter': self.validate_string,
            'font-description-initial': self.validate_string,
            'font-description-name-for-methods': self.validate_string,
            'font-description-setter': self.validate_string,
            'font-property': self.validate_boolean,
            'high-priority': self.validate_boolean,
            'internal-only': self.validate_boolean,
            'logical-property-group': self.validate_logical_property_group,
            'longhands': self.validate_array,
            'parser-exported': self.validate_boolean,
            'parser-function': self.validate_string,
            'parser-function-allows-number-or-integer-input': self.validate_boolean,
            'parser-function-comment': self.validate_comment,
            'parser-grammar': self.validate_string,
            'parser-grammar-comment': self.validate_comment,
            'parser-grammar-unused': self.validate_string,
            'parser-grammar-unused-comment': self.validate_comment,
            'parser-grammar-unused-reason': self.validate_string,
            'parser-shorthand': self.validate_string,
            'render-style-getter': self.validate_string,
            'render-style-initial': self.validate_string,
            'render-style-name-for-methods': self.validate_string,
            'render-style-setter': self.validate_string,
            'runtime-flag': self.validate_string,
            'separator': self.validate_string,
            'settings-flag': self.validate_string,
            'sink-priority': self.validate_boolean,
            'shorthand-pattern': self.validate_string,
            'shorthand-pattern-comment': self.validate_comment,
            'shorthand-parser-pattern': self.validate_string,
            'shorthand-style-extractor-pattern': self.validate_string,
            'skip-codegen': self.validate_boolean,
            'skip-parser': self.validate_boolean,
            'skip-style-builder': self.validate_boolean,
            'skip-style-extractor': self.validate_boolean,
            'skip-style-extractor-comment': self.validate_comment,
            'style-builder-conditional-converter': self.validate_string,
            'style-builder-converter': self.validate_string,
            'style-builder-custom': self.validate_string,
            'style-converter': self.validate_string,
            'style-converter-comment': self.validate_comment,
            'style-extractor-converter': self.validate_string,
            'style-extractor-converter-comment': self.validate_comment,
            'style-extractor-custom': self.validate_boolean,
            'svg': self.validate_boolean,
            'top-priority': self.validate_boolean,
            'top-priority-reason': self.validate_string,
            'visited-link-color-support': self.validate_boolean,
        }

        for key, value in codegen_properties.items():
            if key not in keys_and_validators:
                self._handle_style_error(0, 'json/syntax', 5, 'codegen-properties for property "%s" has unexpected key "%s".' % (property_name, key))
                return

            keys_and_validators[key](property_name, 'codegen-properties', key, value)


class JSONImportExpectationsChecker(JSONChecker):
    """Processes the import-expectations.json lines"""

    def check(self, lines):
        super(JSONImportExpectationsChecker, self).check(lines)

        try:
            expectations = json.loads("\n".join(lines) + "\n")
        except ValueError:
            # Skip the rest, the parent class will have logged for this
            return

        if not isinstance(expectations, dict):
            self._handle_style_error(
                0, "json/syntax", 5, "The top-level data must be an object"
            )
            return

        # This will find all JSON strings, as quotation marks can _only_ appear in
        # strings. Thus, iterating over non-overlapping matches of this regex will give
        # all strings, and it can be applied on a per line basis as strings cannot
        # contain line breaks.
        json_string_re = re.compile(
            u'"(?:[\\x20-\\x21\\x23-\\x5B\\x5D-\U0010FFFF]|\\\\(?:[\\x22\\x5C\\x2F\\x08\\x0C\\x0A\\x0D\\x09]|u[0-9a-fA-F]{4}))*"'
        )

        string_to_lines = defaultdict(set)
        for i, line in enumerate(lines):
            for m in json_string_re.finditer(line):
                string_to_lines[json.loads(m.group(0))].add(i + 1)

        parsed_expectations = {}

        for key, value in expectations.items():
            if len(string_to_lines[key]) == 1:
                line_no = next(iter(string_to_lines[key]))
            else:
                line_no = 0

            valid = True

            # This is an assert because JSON requires it, and thus should be infallible.
            assert isinstance(key, str)

            if key != "web-platform-tests" and not key.startswith(
                "web-platform-tests/"
            ):
                self._handle_style_error(
                    line_no,
                    "json/syntax",
                    5,
                    "Each key must start with 'web-platform-tests/', got '{}'".format(
                        key
                    ),
                )
                valid = False

            if value not in ("import", "skip", "skip-new-directories"):
                self._handle_style_error(
                    line_no,
                    "json/syntax",
                    5,
                    'Each value must be one of "import", "skip", or "skip-new-directories"',
                )
                valid = False

            if valid:
                parsed_expectations[tuple(key.split("/"))] = value

        for parsed_key, value in parsed_expectations.items():
            key = "/".join(parsed_key)

            if len(string_to_lines[key]) == 1:
                line_no = next(iter(string_to_lines[key]))
            else:
                line_no = 0

            if not parsed_key[-1]:
                self._handle_style_error(
                    line_no,
                    "json/syntax",
                    5,
                    "'{}' has trailing slash".format(key),
                )

            if value != "skip-new-directories":
                for i in range(len(parsed_key) - 1, 0, -1):
                    parent_key = parsed_key[:i]
                    if parent_key in parsed_expectations:
                        if value == parsed_expectations[parent_key]:
                            self._handle_style_error(
                                line_no,
                                "json/syntax",
                                5,
                                "'{}' is redundant, '{}' already defines '{}'".format(
                                    key, "/".join(parent_key), value
                                ),
                            )
                        break

            is_skipped = value in ("skip", "skip-new-directories")
            is_prefix = any(
                parsed_key == k[: len(parsed_key)]
                and len(k) > len(parsed_key)
                and v == "import"
                for k, v in parsed_expectations.items()
            )
            should_exist = not is_skipped or is_prefix

            full_path = os.path.join(
                os.path.dirname(self._file_path), "..", *parsed_key
            )
            exists = os.path.exists(full_path)
            if exists != should_exist:
                self._handle_style_error(
                    line_no,
                    "json/syntax",
                    5,
                    "'{}' does {}exist and is {}skipped".format(
                        key,
                        "" if exists else "not ",
                        "" if is_skipped else "not ",
                    ),
                )
            elif should_exist and not os.path.isdir(full_path):
                self._handle_style_error(
                    line_no,
                    "json/syntax",
                    5,
                    "'{}' is not a directory".format(key),
                )
