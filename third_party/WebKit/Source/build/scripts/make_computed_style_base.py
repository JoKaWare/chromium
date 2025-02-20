#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import math
import sys

import json5_generator
import template_expander
import make_style_builder

from name_utilities import camel_case, lower_first


class Field(object):
    """
    The generated ComputedStyle object is made up of a series of Fields.
    Each Field has a name, size, type, etc, and a bunch of attributes to
    determine which methods it will be used in.

    A Field also has enough information to use any storage type in C++, such as
    regular member variables, or more complex storage like vectors or hashmaps.
    Almost all properties will have at least one Field, often more than one.

    Fields also fall into various families, which determine the logic that is
    used to generate them. The available field families are:
      - 'property', for fields that store CSS properties
      - 'inherited_flag', for single-bit flags that store whether a property is
                          inherited by this style or set explicitly
    """

    # List of required attributes for a field which need to be passed in by
    # keyword arguments
    REQUIRED_ATTRIBUTES = set([
        # Name of field
        'name',
        # Name of property field is for
        'property_name',
        # Internal field storage type (storage_type_path can be None)
        'storage_type',
        'storage_type_path',
        # Bits needed for storage
        'size',
        # Default value for field
        'default_value',
        # Method names
        'getter_method_name',
        'setter_method_name',
        'initial_method_name',
        'resetter_method_name',
    ])

    def __init__(self, field_family, **kwargs):
        # Values common to all fields
        # Set attributes from the keyword arguments
        for attrib in Field.REQUIRED_ATTRIBUTES:
            setattr(self, attrib, kwargs.pop(attrib))

        # Field family: one of these must be true
        self.is_property = field_family == 'property'
        self.is_inherited_flag = field_family == 'inherited_flag'
        assert (self.is_property, self.is_inherited_flag).count(True) == 1, \
            'Field family has to be exactly one of: property, inherited_flag'

        if self.is_property:
            self.is_inherited = kwargs.pop('inherited')
            self.is_independent = kwargs.pop('independent')
            assert self.is_inherited or not self.is_independent, 'Only inherited fields can be independent'

            self.is_inherited_method_name = kwargs.pop('is_inherited_method_name')
        elif self.is_inherited_flag:
            # Inherited flag-only fields
            pass

        assert len(kwargs) == 0, 'Unexpected arguments provided to Field: ' + str(kwargs)


def _create_enums(properties):
    """
    Returns a dictionary of enums to be generated, enum name -> [list of enum values]
    """
    enums = {}
    for property_ in properties:
        # Only generate enums for keyword properties that use the default field_storage_type.
        if property_['keyword_only'] and property_['field_storage_type'] is None:
            enum_name = property_['type_name']
            # From the Blink style guide: Enum members should use InterCaps with an initial capital letter. [names-enum-members]
            enum_values = [('k' + camel_case(k)) for k in property_['keywords']]

            if enum_name in enums:
                # There's an enum with the same name, check if the enum values are the same
                assert set(enums[enum_name]) == set(enum_values), \
                    ("'" + property_['name'] + "' can't have type_name '" + enum_name + "' "
                     "because it was used by a previous property, but with a different set of keywords. "
                     "Either give it a different name or ensure the keywords are the same.")

            enums[enum_name] = enum_values

    return enums


def _create_property_field(property_):
    """
    Create a property field from a CSS property and return the Field object.
    """
    property_name = property_['name_for_methods']
    property_name_lower = lower_first(property_name)

    # From the Blink style guide: Other data members should be prefixed by "m_". [names-data-members]
    field_name = 'm_' + property_name_lower
    bits_needed = math.log(len(property_['keywords']), 2)

    # Separate the type path from the type name, if specified.
    if property_['field_storage_type']:
        type_path = property_['field_storage_type']
        type_name = type_path.split('/')[-1]
    else:
        type_name = property_['type_name']
        type_path = None

    # For now, the getter name should match the field name. Later, getter names
    # will start with an uppercase letter, so if they conflict with the type name,
    # add 'get' to the front.
    getter_method_name = property_name_lower
    if type_name == property_name:
        getter_method_name = 'get' + property_name

    assert property_['initial_keyword'] is not None, \
        ('MakeComputedStyleBase requires an initial keyword for keyword_only values, none specified '
         'for property ' + property_['name'])
    default_value = type_name + '::k' + camel_case(property_['initial_keyword'])

    # Add the property itself as a member variable.
    return Field(
        'property',
        name=field_name,
        property_name=property_['name'],
        inherited=property_['inherited'],
        independent=property_['independent'],
        storage_type=type_name,
        storage_type_path=type_path,
        size=int(math.ceil(bits_needed)),
        default_value=default_value,
        getter_method_name=getter_method_name,
        setter_method_name='set' + property_name,
        initial_method_name='initial' + property_name,
        resetter_method_name='reset' + property_name,
        is_inherited_method_name=property_name_lower + 'IsInherited',
    )


def _create_inherited_flag_field(property_):
    """
    Create the field used for an inheritance fast path from an independent CSS property,
    and return the Field object.
    """
    property_name = property_['name_for_methods']
    property_name_lower = lower_first(property_name)

    field_name_suffix_upper = property_name + 'IsInherited'
    field_name_suffix_lower = property_name_lower + 'IsInherited'

    return Field(
        'inherited_flag',
        name='m_' + field_name_suffix_lower,
        property_name=property_['name'],
        storage_type='bool',
        storage_type_path=None,
        size=1,
        default_value='true',
        getter_method_name=field_name_suffix_lower,
        setter_method_name='set' + field_name_suffix_upper,
        initial_method_name='initial' + field_name_suffix_upper,
        resetter_method_name='reset' + field_name_suffix_upper,
    )


def _create_fields(properties):
    """
    Create ComputedStyle fields from CSS properties and return a list of Field objects.
    """
    fields = []
    for property_ in properties:
        # Keywords only means we generate an enum field.
        if property_['keyword_only']:
            # If the property is independent, add the single-bit sized isInherited flag
            # to the list of Fields as well.
            if property_['independent']:
                fields.append(_create_inherited_flag_field(property_))

            fields.append(_create_property_field(property_))

    return fields


def _pack_fields(fields):
    """
    Group a list of fields into buckets to minimise padding.
    Returns a list of buckets, where each bucket is a list of Field objects.
    """
    # Since fields cannot cross word boundaries, in order to minimize
    # padding, group fields into buckets so that as many buckets as possible
    # are exactly 32 bits. Although this greedy approach may not always
    # produce the optimal solution, we add a static_assert to the code to
    # ensure ComputedStyleBase results in the expected size. If that
    # static_assert fails, this code is falling into the small number of
    # cases that are suboptimal, and may need to be rethought.
    # For more details on packing bitfields to reduce padding, see:
    # http://www.catb.org/esr/structure-packing/#_bitfields
    field_buckets = []
    # Consider fields in descending order of size to reduce fragmentation
    # when they are selected.
    for field in sorted(fields, key=lambda f: f.size, reverse=True):
        added_to_bucket = False
        # Go through each bucket and add this field if it will not increase
        # the bucket's size to larger than 32 bits. Otherwise, make a new
        # bucket containing only this field.
        for bucket in field_buckets:
            if sum(f.size for f in bucket) + field.size <= 32:
                bucket.append(field)
                added_to_bucket = True
                break
        if not added_to_bucket:
            field_buckets.append([field])

    return field_buckets


class ComputedStyleBaseWriter(make_style_builder.StyleBuilderWriter):
    def __init__(self, json5_file_path):
        super(ComputedStyleBaseWriter, self).__init__(json5_file_path)
        self._outputs = {
            'ComputedStyleBase.h': self.generate_base_computed_style_h,
            'ComputedStyleBase.cpp': self.generate_base_computed_style_cpp,
            'ComputedStyleBaseConstants.h': self.generate_base_computed_style_constants,
        }

        # Create all the enums used by properties
        self._generated_enums = _create_enums(self._properties.values())

        # Create all the fields
        all_fields = _create_fields(self._properties.values())

        # Group fields into buckets
        field_buckets = _pack_fields(all_fields)

        # The expected size of ComputedStyleBase is equivalent to as many words
        # as the total number of buckets.
        self._expected_total_field_bytes = len(field_buckets)

        # The most optimal size of ComputedStyleBase is the total sum of all the
        # field sizes, rounded up to the nearest word. If this produces the
        # incorrect value, either the packing algorithm is not optimal or there
        # is no way to pack the fields such that excess padding space is not
        # added.
        # If this fails, increase extra_padding_bytes by 1, but be aware that
        # this also increases ComputedStyleBase by 1 word.
        # We should be able to bring extra_padding_bytes back to 0 from time to
        # time.
        extra_padding_bytes = 0
        optimal_total_field_bytes = int(math.ceil(sum(f.size for f in all_fields) / 32.0))
        real_total_field_bytes = optimal_total_field_bytes + extra_padding_bytes
        assert self._expected_total_field_bytes == real_total_field_bytes, \
            ('The field packing algorithm produced %s bytes, optimal is %s bytes' %
             (self._expected_total_field_bytes, real_total_field_bytes))

        # Order the fields so fields in each bucket are adjacent.
        self._fields = []
        for bucket in field_buckets:
            for field in bucket:
                self._fields.append(field)


    @template_expander.use_jinja('ComputedStyleBase.h.tmpl')
    def generate_base_computed_style_h(self):
        return {
            'properties': self._properties,
            'enums': self._generated_enums,
            'fields': self._fields,
            'expected_total_field_bytes': self._expected_total_field_bytes,
        }

    @template_expander.use_jinja('ComputedStyleBase.cpp.tmpl')
    def generate_base_computed_style_cpp(self):
        return {
            'properties': self._properties,
            'enums': self._generated_enums,
            'fields': self._fields,
            'expected_total_field_bytes': self._expected_total_field_bytes,
        }

    @template_expander.use_jinja('ComputedStyleBaseConstants.h.tmpl')
    def generate_base_computed_style_constants(self):
        return {
            'properties': self._properties,
            'enums': self._generated_enums,
            'fields': self._fields,
            'expected_total_field_bytes': self._expected_total_field_bytes,
        }

if __name__ == '__main__':
    json5_generator.Maker(ComputedStyleBaseWriter).main()
