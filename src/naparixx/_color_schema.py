"""Declaration transport only for handwritten schema factory ownership."""

from __future__ import annotations

from naparixx import _native


def _schema_0(*, when_used="json-unless-none"):
    _source_locals = (...).__call__(0, when_used); del when_used; return (...).__call__(_source_locals)  # noqa: E501, E702  # One declaration transport line.


def _schema_1(function, *, field_name=None, ref=None, json_schema_input_schema=None, metadata=None, serialization=None):
    _source_locals = (...).__call__(1, function, field_name, ref, json_schema_input_schema, metadata, serialization); del function, field_name, ref, json_schema_input_schema, metadata, serialization; return (...).__call__(_source_locals)  # noqa: E501, E702  # One declaration transport line.


def _schema_2(**kwargs):
    _source_locals = (...).__call__(2, kwargs); del kwargs; return (...).__call__(_source_locals)  # noqa: E501, E702  # One declaration transport line.


_SHELLS = (_schema_0, _schema_1, _schema_2)

# Immutable compiler controls only, never an executable original CodeType.
_PROFILES = ((11,
  ((('INT', 0),
    ('INT', 0),
    ('INT', 1),
    ('INT', 2),
    ('INT', 3),
    ('INT', 16777219),
    ('BYTES',
     '97007401000000000000000000006401ac02a6010000ab0100000000000000007d017c0064036b030000000072057c007c0164043c0000007c015300'),
    ('TUPLE',
     (('STR',
       '\n'
       "    Returns a schema for serialization using python's `str()` / `__str__` "
       'method.\n'
       '\n'
       '    Args:\n'
       '        when_used: Same meaning as for [general_function_plain_ser_schema], '
       'but with a different default\n'
       '    '),
      ('STR', 'to-string'),
      ('TUPLE', (('STR', 'type'),)),
      ('STR', 'json-unless-none'),
      ('STR', 'when_used'))),
    ('TUPLE', (('STR', 'dict'),)),
    ('TUPLE', (('STR', 'when_used'), ('STR', 's'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'to_string_ser_schema'),
    ('STR', 'to_string_ser_schema'),
    ('INT', 455),
    ('BYTES', '')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 5),
    ('INT', 6),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '97007c01811c7401000000000000000000006a01000000000000000064027404000000000000000000006403ac04a6030000ab0300000000000000000100740700000000000000000000640574070000000000000000000064067c007c01ac07a6030000ab0300000000000000007c027c037c047c05ac08a6060000ab0600000000000000005300'),
    ('TUPLE',
     (('STR',
       '\n'
       '    Returns a schema that uses the provided function for validation, an `info` '
       'argument is passed, e.g.:\n'
       '\n'
       '    ```py\n'
       '    from pydantic_core import SchemaValidator, core_schema\n'
       '\n'
       '    def fn(v: str, info: core_schema.ValidationInfo) -> str:\n'
       "        assert 'hello' in v\n"
       "        return v + 'world'\n"
       '\n'
       '    schema = core_schema.with_info_plain_validator_function(function=fn)\n'
       '    v = SchemaValidator(schema)\n'
       "    assert v.validate_python('hello ') == 'hello world'\n"
       '    ```\n'
       '\n'
       '    Args:\n'
       '        function: The validator function to call\n'
       '        field_name: The name of the field this validator is applied to, if any '
       '(deprecated)\n'
       '        ref: optional unique identifier of the schema, used to reference the '
       'schema in other places\n'
       '        json_schema_input_schema: The core schema to be used to generate the '
       'corresponding JSON Schema input type\n'
       '        metadata: Any other information you want to include with the schema, '
       'not used by pydantic-core\n'
       '        serialization: Custom serialization schema\n'
       '    '),
      ('NONE',),
      ('STR',
       'The `field_name` argument on `with_info_plain_validator_function` is '
       'deprecated, it will be passed to the function through `ValidationState` '
       'instead.'),
      ('INT', 2),
      ('TUPLE', (('STR', 'stacklevel'),)),
      ('STR', 'function-plain'),
      ('STR', 'with-info'),
      ('TUPLE', (('STR', 'type'), ('STR', 'function'), ('STR', 'field_name'))),
      ('TUPLE',
       (('STR', 'type'),
        ('STR', 'function'),
        ('STR', 'ref'),
        ('STR', 'json_schema_input_schema'),
        ('STR', 'metadata'),
        ('STR', 'serialization'))))),
    ('TUPLE',
     (('STR', 'warnings'),
      ('STR', 'warn'),
      ('STR', 'DeprecationWarning'),
      ('STR', '_dict_not_none'))),
    ('TUPLE',
     (('STR', 'function'),
      ('STR', 'field_name'),
      ('STR', 'ref'),
      ('STR', 'json_schema_input_schema'),
      ('STR', 'metadata'),
      ('STR', 'serialization'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'with_info_plain_validator_function'),
    ('STR', 'with_info_plain_validator_function'),
    ('INT', 2987),
    ('BYTES', '')),
   (('INT', 0),
    ('INT', 0),
    ('INT', 0),
    ('INT', 1),
    ('INT', 3),
    ('INT', 16777227),
    ('BYTES',
     '9700640184007c00a0000000000000000000000000000000000000000000a6000000ab0000000000000000004400a6000000ab0000000000000000005300'),
    ('TUPLE',
     (('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 3),
        ('INT', 4),
        ('INT', 16777235),
        ('BYTES', '970069007c005d0a5c0200007d017d027c02ae077c017c0293028c0b5300'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'k'), ('STR', 'v'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<dictcomp>'),
        ('STR', '_dict_not_none.<locals>.<dictcomp>'),
        ('INT', 5013),
        ('BYTES', ''))))),
    ('TUPLE', (('STR', 'items'),)),
    ('TUPLE', (('STR', 'kwargs'),)),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_dict_not_none'),
    ('STR', '_dict_not_none'),
    ('INT', 5012),
    ('BYTES', '')))),
 (12,
  ((('INT', 0),
    ('INT', 0),
    ('INT', 1),
    ('INT', 2),
    ('INT', 3),
    ('INT', 16777219),
    ('BYTES',
     '9700740100000000000000006401ac02ab010000000000007d017c0064036b37000072057c007c0164043c0000007c015300'),
    ('TUPLE',
     (('STR',
       '\n'
       "    Returns a schema for serialization using python's `str()` / `__str__` "
       'method.\n'
       '\n'
       '    Args:\n'
       '        when_used: Same meaning as for [general_function_plain_ser_schema], '
       'but with a different default\n'
       '    '),
      ('STR', 'to-string'),
      ('TUPLE', (('STR', 'type'),)),
      ('STR', 'json-unless-none'),
      ('STR', 'when_used'))),
    ('TUPLE', (('STR', 'dict'),)),
    ('TUPLE', (('STR', 'when_used'), ('STR', 's'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'to_string_ser_schema'),
    ('STR', 'to_string_ser_schema'),
    ('INT', 455),
    ('BYTES', '')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 5),
    ('INT', 6),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '97007c01811c740100000000000000006a020000000000000000000000000000000000006401740400000000000000006402ac03ab0300000000000001007407000000000000000064047407000000000000000064057c007c01ac06ab030000000000007c027c037c047c05ac07ab060000000000005300'),
    ('TUPLE',
     (('STR',
       '\n'
       '    Returns a schema that uses the provided function for validation, an `info` '
       'argument is passed, e.g.:\n'
       '\n'
       '    ```py\n'
       '    from pydantic_core import SchemaValidator, core_schema\n'
       '\n'
       '    def fn(v: str, info: core_schema.ValidationInfo) -> str:\n'
       "        assert 'hello' in v\n"
       "        return v + 'world'\n"
       '\n'
       '    schema = core_schema.with_info_plain_validator_function(function=fn)\n'
       '    v = SchemaValidator(schema)\n'
       "    assert v.validate_python('hello ') == 'hello world'\n"
       '    ```\n'
       '\n'
       '    Args:\n'
       '        function: The validator function to call\n'
       '        field_name: The name of the field this validator is applied to, if any '
       '(deprecated)\n'
       '        ref: optional unique identifier of the schema, used to reference the '
       'schema in other places\n'
       '        json_schema_input_schema: The core schema to be used to generate the '
       'corresponding JSON Schema input type\n'
       '        metadata: Any other information you want to include with the schema, '
       'not used by pydantic-core\n'
       '        serialization: Custom serialization schema\n'
       '    '),
      ('STR',
       'The `field_name` argument on `with_info_plain_validator_function` is '
       'deprecated, it will be passed to the function through `ValidationState` '
       'instead.'),
      ('INT', 2),
      ('TUPLE', (('STR', 'stacklevel'),)),
      ('STR', 'function-plain'),
      ('STR', 'with-info'),
      ('TUPLE', (('STR', 'type'), ('STR', 'function'), ('STR', 'field_name'))),
      ('TUPLE',
       (('STR', 'type'),
        ('STR', 'function'),
        ('STR', 'ref'),
        ('STR', 'json_schema_input_schema'),
        ('STR', 'metadata'),
        ('STR', 'serialization'))))),
    ('TUPLE',
     (('STR', 'warnings'),
      ('STR', 'warn'),
      ('STR', 'DeprecationWarning'),
      ('STR', '_dict_not_none'))),
    ('TUPLE',
     (('STR', 'function'),
      ('STR', 'field_name'),
      ('STR', 'ref'),
      ('STR', 'json_schema_input_schema'),
      ('STR', 'metadata'),
      ('STR', 'serialization'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'with_info_plain_validator_function'),
    ('STR', 'with_info_plain_validator_function'),
    ('INT', 2987),
    ('BYTES', '')),
   (('INT', 0),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 6),
    ('INT', 16777227),
    ('BYTES',
     '97007c006a01000000000000000000000000000000000000ab0000000000000044008f018f026303690063025d0b00005c0200007d017d027c0280018c097c017c0293028c0d040063037d027d0153006302010063037d027d017700'),
    ('TUPLE', (('NONE',),)),
    ('TUPLE', (('STR', 'items'),)),
    ('TUPLE', (('STR', 'kwargs'), ('STR', 'k'), ('STR', 'v'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_dict_not_none'),
    ('STR', '_dict_not_none'),
    ('INT', 5012),
    ('BYTES', '940a28069f052806')))),
 (13,
  ((('INT', 0),
    ('INT', 0),
    ('INT', 1),
    ('INT', 2),
    ('INT', 4),
    ('INT', 16777219),
    ('BYTES',
     '95005b0100000000000000005301530239016e01550053033a77000061040000580153042700000055012400'),
    ('TUPLE',
     (('STR',
       '\n'
       "Returns a schema for serialization using python's `str()` / `__str__` method.\n"
       '\n'
       'Args:\n'
       '    when_used: Same meaning as for [general_function_plain_ser_schema], but '
       'with a different default\n'),
      ('STR', 'to-string'),
      ('TUPLE', (('STR', 'type'),)),
      ('STR', 'json-unless-none'),
      ('STR', 'when_used'))),
    ('TUPLE', (('STR', 'dict'),)),
    ('TUPLE', (('STR', 'when_used'), ('STR', 's'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'to_string_ser_schema'),
    ('STR', 'to_string_ser_schema'),
    ('INT', 455),
    ('BYTES', '')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 5),
    ('INT', 6),
    ('INT', 9),
    ('INT', 16777219),
    ('BYTES',
     '95005501621a00005b0000000000000000005202000000000000000000000000000000000000220053015b04000000000000000053025303390320005b07000000000000000053045b07000000000000000053055801530639035502550355045505530739062400'),
    ('TUPLE',
     (('STR',
       '\n'
       'Returns a schema that uses the provided function for validation, an `info` '
       'argument is passed, e.g.:\n'
       '\n'
       '```py\n'
       'from pydantic_core import SchemaValidator, core_schema\n'
       '\n'
       'def fn(v: str, info: core_schema.ValidationInfo) -> str:\n'
       "    assert 'hello' in v\n"
       "    return v + 'world'\n"
       '\n'
       'schema = core_schema.with_info_plain_validator_function(function=fn)\n'
       'v = SchemaValidator(schema)\n'
       "assert v.validate_python('hello ') == 'hello world'\n"
       '```\n'
       '\n'
       'Args:\n'
       '    function: The validator function to call\n'
       '    field_name: The name of the field this validator is applied to, if any '
       '(deprecated)\n'
       '    ref: optional unique identifier of the schema, used to reference the '
       'schema in other places\n'
       '    json_schema_input_schema: The core schema to be used to generate the '
       'corresponding JSON Schema input type\n'
       '    metadata: Any other information you want to include with the schema, not '
       'used by pydantic-core\n'
       '    serialization: Custom serialization schema\n'),
      ('STR',
       'The `field_name` argument on `with_info_plain_validator_function` is '
       'deprecated, it will be passed to the function through `ValidationState` '
       'instead.'),
      ('INT', 2),
      ('TUPLE', (('STR', 'stacklevel'),)),
      ('STR', 'function-plain'),
      ('STR', 'with-info'),
      ('TUPLE', (('STR', 'type'), ('STR', 'function'), ('STR', 'field_name'))),
      ('TUPLE',
       (('STR', 'type'),
        ('STR', 'function'),
        ('STR', 'ref'),
        ('STR', 'json_schema_input_schema'),
        ('STR', 'metadata'),
        ('STR', 'serialization'))))),
    ('TUPLE',
     (('STR', 'warnings'),
      ('STR', 'warn'),
      ('STR', 'DeprecationWarning'),
      ('STR', '_dict_not_none'))),
    ('TUPLE',
     (('STR', 'function'),
      ('STR', 'field_name'),
      ('STR', 'ref'),
      ('STR', 'json_schema_input_schema'),
      ('STR', 'metadata'),
      ('STR', 'serialization'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'with_info_plain_validator_function'),
    ('STR', 'with_info_plain_validator_function'),
    ('INT', 2987),
    ('BYTES', '')),
   (('INT', 0),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 6),
    ('INT', 16777227),
    ('BYTES',
     '95005500520100000000000000000000000000000000000035000000000000001300560156027303300073021300480c00007502000070125502630200004d0a000058125f024d0e00000b00200073036e026e0124007302200073036e026e016600'),
    ('TUPLE', (('NONE',),)),
    ('TUPLE', (('STR', 'items'),)),
    ('TUPLE', (('STR', 'kwargs'), ('STR', 'k'), ('STR', 'v'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_dict_not_none'),
    ('STR', '_dict_not_none'),
    ('INT', 5012),
    ('BYTES', '94092b06a1062b06')))))


def bind(admission):
    return _native.schema_factory_bind(admission, _SHELLS, _PROFILES)
