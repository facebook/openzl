import pytest
from mkdocstrings_handlers.zstd.doxygen import (
    Compound,
    CompoundType,
    DescriptionAdmonition,
    DescriptionKind,
    DescriptionList,
    DescriptionParagraph,
    DescriptionParameter,
    DescriptionReturn,
    DescriptionText,
    Function,
    ObjectKind,
    Parameter,
    ParameterDirection,
)


def test_doxygen_file1(doxygen):
    macro1 = doxygen.collect("MACRO1")
    assert macro1.kind == ObjectKind.DEFINE, f"{macro1}"
    assert macro1.name == "MACRO1"
    assert macro1.initializer == "x"
    assert len(macro1.parameters) == 1
    assert macro1.parameters[0].type is None
    assert macro1.parameters[0].name == "x"

    func1 = doxygen.collect("func1")
    assert func1.kind == ObjectKind.FUNCTION
    assert func1.name == "func1"
    assert len(func1.parameters) == 2
    assert func1.parameters[0].type == "struct1*"
    assert func1.parameters[0].name == "s"
    assert func1.parameters[1].type == "int"
    assert func1.parameters[1].name == "x"

    enum1 = doxygen.collect("enum1")
    assert enum1.kind == ObjectKind.ENUM
    assert enum1.name == "enum1"
    assert len(enum1.values) == 4
    assert enum1.values[0].name == "enum1_value1"
    assert enum1.values[0].initializer == "= 0"
    assert enum1.values[1].name == "enum1_value2"
    assert enum1.values[1].initializer is None
    assert enum1.values[3].name == "enum1_value5"
    assert enum1.values[3].initializer == "= 5"


def test_doxygen_file2(doxygen):
    macro2 = doxygen.collect("MACRO2")
    assert macro2.kind == ObjectKind.DEFINE
    assert macro2.name == "MACRO2"
    assert macro2.initializer == "MACRO3(x)"
    assert len(macro2.parameters) == 1
    assert macro2.parameters[0].type is None
    assert macro2.parameters[0].name == "x"

    func2 = doxygen.collect("func2")
    assert func2.kind == ObjectKind.FUNCTION
    assert func2.name == "func2"
    assert len(func2.parameters) == 2
    assert func2.parameters[0].type == "struct2*"
    assert func2.parameters[0].name == "s"
    assert func2.parameters[1].type == "int"
    assert func2.parameters[1].name == "x"

    enum2 = doxygen.collect("enum2")
    assert enum2.kind == ObjectKind.ENUM
    assert enum2.name == "enum2"
    assert len(enum2.values) == 4
    assert enum2.values[0].name == "enum2_value1"
    assert enum2.values[0].initializer == "= 0"
    assert enum2.values[1].name == "enum2_value2"
    assert enum2.values[1].initializer is None
    assert enum2.values[3].name == "enum2_value5"
    assert enum2.values[3].initializer == "= 5"


def test_doxygen_file3(doxygen):
    macro3 = doxygen.collect("MACRO3")
    assert macro3.kind == ObjectKind.DEFINE
    assert macro3.name == "MACRO3"
    assert macro3.initializer == "MACRO1(x)"
    assert len(macro3.parameters) == 1
    assert macro3.parameters[0].type is None
    assert macro3.parameters[0].name == "x"

    func3 = doxygen.collect("func3")
    assert func3.kind == ObjectKind.FUNCTION
    assert func3.name == "func3"
    assert len(func3.parameters) == 2
    assert func3.parameters[0].type == "struct3*"
    assert func3.parameters[0].name == "s"
    assert func3.parameters[1].type == "int"
    assert func3.parameters[1].name == "x"

    var3 = doxygen.collect("var3")
    assert var3.kind == ObjectKind.VARIABLE
    assert var3.name == "var3"
    assert var3.type == "const int"
    assert var3.initializer == "= 5"

    enum3 = doxygen.collect("enum3")
    assert enum3.kind == ObjectKind.ENUM
    assert enum3.name == "enum3"
    assert len(enum3.values) == 4
    assert enum3.values[0].name == "enum3_value1"
    assert enum3.values[0].initializer == "= 0"
    assert enum3.values[1].name == "enum3_value2"
    assert enum3.values[1].initializer is None
    assert enum3.values[3].name == "enum3_value5"
    assert enum3.values[3].initializer == "= 5"


def test_struct(doxygen):
    s = doxygen.collect("s1")
    assert s.kind == ObjectKind.COMPOUND
    assert s.type == CompoundType.STRUCT
    assert s.name == "s1"
    assert len(s.members) == 3

    assert s.members[0].kind == ObjectKind.VARIABLE
    assert s.members[0].type == "int"
    assert s.members[0].name == "x"
    assert s.members[0].qualified_name == "s1::x"

    assert s.members[1].kind == ObjectKind.VARIABLE
    assert s.members[1].type == "enum1"
    assert s.members[1].name == "e"
    assert s.members[1].qualified_name == "s1::e"

    assert s.members[2].kind == ObjectKind.VARIABLE
    assert s.members[2].qualified_name == "s1::y"


def test_union(doxygen):
    u = doxygen.collect("u1")
    assert u.kind == ObjectKind.COMPOUND
    assert u.type == CompoundType.UNION
    assert u.name == "u1"
    assert len(u.members) == 2

    assert u.members[0].kind == ObjectKind.VARIABLE
    assert u.members[0].type == "int"
    assert u.members[0].name == "x"
    assert u.members[0].qualified_name == "u1::x"

    assert u.members[1].kind == ObjectKind.VARIABLE
    assert u.members[1].type == "s1"
    assert u.members[1].name == "s"
    assert u.members[1].qualified_name == "u1::s"


def test_group(doxygen):
    g = doxygen.collect("g1")
    assert g.kind == ObjectKind.COMPOUND
    assert g.type == CompoundType.GROUP
    assert g.name == "g1"
    assert g.title == "Group 1"

    assert g.members[0].kind == ObjectKind.COMPOUND
    assert g.members[0].type == CompoundType.STRUCT
    assert g.members[0].name == "g1_struct"
    assert g.members[0].qualified_name == "g1_struct"

    assert g.members[1].kind == ObjectKind.COMPOUND
    assert g.members[1].type == CompoundType.UNION
    assert g.members[1].name == "g1_union"
    assert g.members[1].qualified_name == "g1_union"
    assert g.members[1].members[0].type == "struct g1_struct"

    assert g.members[2].kind == ObjectKind.ENUM
    assert g.members[2].name == "g1_enum"
    assert g.members[2].qualified_name == "g1_enum"

    assert g.members[3].kind == ObjectKind.VARIABLE
    assert g.members[3].type == "const int"
    assert g.members[3].name == "x"
    assert g.members[3].qualified_name == "x"

    assert g.members[4].kind == ObjectKind.FUNCTION
    assert g.members[4].name == "g1_func"
    assert g.members[4].qualified_name == "g1_func"

    assert g.members[5].kind == ObjectKind.DEFINE
    assert g.members[5].name == "G1_MACRO"
    assert g.members[5].initializer == "5"
    assert g.members[5].qualified_name == "G1_MACRO"


def test_func_in_para_returns(doxygen):
    func = doxygen.collect("func_in_para_returns")
    desc = func.description

    print(desc)

    expected = DescriptionParagraph(
        contents=[
            DescriptionText(
                contents="This is some inline documentation that goes straight into a return without a newline."
            ),
            DescriptionReturn(
                description=DescriptionParagraph(
                    contents=[DescriptionText(contents="Something with a multiline")]
                ),
                title="Returns",
            ),
            DescriptionAdmonition(
                style="note",
                title="Note",
                contents=DescriptionParagraph(
                    contents=[DescriptionText(contents="This is an important note")]
                ),
            ),
            DescriptionAdmonition(
                style="warning",
                title="Warning",
                contents=DescriptionParagraph(
                    contents=[
                        DescriptionText(contents="Followed by a very important warning")
                    ]
                ),
            ),
            DescriptionText(contents="Finally some text"),
            DescriptionList(
                title="Parameters",
                contents=[
                    DescriptionParameter(
                        name="x",
                        description=DescriptionParagraph(
                            contents=[DescriptionText(contents="This is a param")]
                        ),
                        type="int",
                        direction=None,
                    ),
                    DescriptionParameter(
                        name="y",
                        description=DescriptionParagraph(
                            contents=[DescriptionText(contents="This is another param")]
                        ),
                        type=None,
                        direction=ParameterDirection.OUT,
                    ),
                    DescriptionParameter(
                        name="z",
                        description=DescriptionParagraph(
                            contents=[DescriptionText(contents="Finally a 3rd param")]
                        ),
                        type=None,
                        direction=None,
                    ),
                ],
            ),
            DescriptionText(contents="Followed by some more text"),
        ]
    )
    assert desc == expected


def test_var_list_items(doxygen):
    var = doxygen.collect("var_list_items")
    desc = var.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionParagraph(
                contents=[
                    DescriptionText(contents="text"),
                    DescriptionList(
                        title=None,
                        contents=[
                            DescriptionParagraph(
                                contents=[DescriptionText(contents="item1")]
                            ),
                            DescriptionParagraph(
                                contents=[DescriptionText(contents="item2")]
                            ),
                        ],
                    ),
                ]
            ),
            DescriptionParagraph(contents=[DescriptionText(contents="more")]),
            DescriptionParagraph(
                contents=[
                    DescriptionList(
                        title=None,
                        contents=[
                            DescriptionParagraph(
                                contents=[DescriptionText(contents="item1")]
                            )
                        ],
                    )
                ]
            ),
        ]
    )
    assert desc == expected


def test_var_brief(doxygen):
    var = doxygen.collect("var_brief")
    desc = var.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[DescriptionText(contents="brief description")]
    )
    assert desc == expected


def test_var_brief_and_detailed(doxygen):
    var = doxygen.collect("var_brief_and_detailed")
    desc = var.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionParagraph(
                contents=[DescriptionText(contents="brief description")]
            ),
            DescriptionParagraph(
                contents=[DescriptionText(contents="Followed by a longer description")]
            ),
        ]
    )
    assert desc == expected


def test_var_markups(doxygen):
    var = doxygen.collect("var_markups")
    desc = var.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionText(
                contents="Test markups like <b>bold</b> and <em>italics</em> work as expected."
            )
        ]
    )
    assert desc == expected


def test_func_with_param(doxygen):
    func = doxygen.collect("func_with_param")
    desc = func.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionList(
                title="Parameters",
                contents=[
                    DescriptionParameter(
                        name="x",
                        description=DescriptionParagraph(
                            contents=[DescriptionText(contents="has a description")]
                        ),
                        type="int",
                        direction=None,
                    )
                ],
            )
        ]
    )
    assert desc == expected


def test_func_with_code_block(doxygen):
    func = doxygen.collect("func_with_code_block")
    desc = func.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionText(
                contents="```cpp\n// This is a code block.\ndocs_with_code_block();\nvoid foo();\n```"
            )
        ]
    )
    assert desc == expected


def test_func_with_refs(doxygen):
    func = doxygen.collect("func_with_refs")
    desc = func.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionText(
                contents="[struct1][struct1] [typedef1][typedef1] [s1][s1] [u1][u1] [Group 1][g1] [func1][func1] [enum1][enum1] [x][x] [MACRO1][MACRO1]"
            )
        ]
    )
    assert desc == expected


def test_struct1(doxygen):
    struct1 = doxygen.collect("struct1")
    assert struct1.kind == ObjectKind.TYPEDEF
    assert struct1.type == "struct struct1_s"
    assert struct1.name == "struct1"
    assert struct1.qualified_name == "struct1"
    assert struct1.definition == "typedef struct struct1_s struct1"
    assert struct1.description is None


def test_typedef1(doxygen):
    typedef1 = doxygen.collect("typedef1")
    assert typedef1.kind == ObjectKind.TYPEDEF
    assert typedef1.type == "struct original"
    assert typedef1.name == "typedef1"
    assert typedef1.qualified_name == "typedef1"
    assert typedef1.definition == "typedef struct original typedef1"
    assert typedef1.description.kind == DescriptionKind.PARAGRAPH
    assert len(typedef1.description.contents) == 1


def test_ptr1(doxygen):
    typedef1 = doxygen.collect("ptr1")
    assert typedef1.kind == ObjectKind.TYPEDEF
    assert typedef1.type == "typedef1*"
    assert typedef1.name == "ptr1"
    assert typedef1.qualified_name == "ptr1"
    assert typedef1.definition == "typedef typedef1* ptr1"
    assert typedef1.description.kind == DescriptionKind.PARAGRAPH
    assert len(typedef1.description.contents) == 1


def test_class5(doxygen):
    class5 = doxygen.collect("ns5::Class5")
    assert class5.kind == ObjectKind.COMPOUND
    assert class5.type == CompoundType.CLASS

    member_names = {m.name for m in class5.members}
    assert "Type5" in member_names
    assert "Class5" in member_names
    assert "publicInt" in member_names
    assert "NestedStruct5" in member_names
    assert "overload" in member_names
    assert "NestedEnum5" in member_names
    assert "protectedInt" not in member_names
    assert "privateInt" not in member_names

    constructor = doxygen.collect("ns5::Class5::Class5")
    assert constructor.kind == ObjectKind.OVERLOAD_SET
    assert len(constructor.overloads) == 3
    assert constructor.overloads[0].kind == ObjectKind.FUNCTION
    assert constructor.overloads[0].name == "Class5"
    assert constructor.overloads[0].qualified_name == "ns5::Class5::Class5"
    assert constructor.overloads[0].type == ""
    assert constructor.overloads[0].parameters == []
    assert constructor.overloads[1].kind == ObjectKind.FUNCTION
    assert constructor.overloads[1].name == "Class5"
    assert constructor.overloads[1].qualified_name == "ns5::Class5::Class5"
    assert constructor.overloads[1].type == ""
    assert constructor.overloads[1].parameters[0].type == "const Class5&"

    operatorEqual = doxygen.collect("ns5::Class5::operator=")
    assert operatorEqual.kind == ObjectKind.OVERLOAD_SET
    assert len(operatorEqual.overloads) == 2
    assert operatorEqual.overloads[1].type == "Class5&"
    assert operatorEqual.overloads[1].name == "operator="
    assert operatorEqual.overloads[1].qualified_name == "ns5::Class5::operator="
    assert operatorEqual.overloads[1].parameters[0].type == "Class5&&"

    overload = doxygen.collect("ns5::Class5::overload")
    assert overload.kind == ObjectKind.OVERLOAD_SET
    assert len(overload.overloads) == 2

    publicInt = doxygen.collect("ns5::Class5::publicInt")
    assert publicInt.kind == ObjectKind.VARIABLE
    assert publicInt.name == "publicInt"

    type5 = doxygen.collect("ns5::Class5::Type5")
    assert type5.kind == ObjectKind.TYPEDEF
    assert type5.name == "Type5"
    assert type5.qualified_name == "ns5::Class5::Type5"

    nestedStruct5 = doxygen.collect("ns5::Class5::NestedStruct5")
    assert nestedStruct5.kind == ObjectKind.COMPOUND
    assert nestedStruct5.type == CompoundType.STRUCT
    assert nestedStruct5.name == "NestedStruct5"
    assert nestedStruct5.qualified_name == "ns5::Class5::NestedStruct5"

    nestedEnum5 = doxygen.collect("ns5::Class5::NestedEnum5")
    assert nestedEnum5.kind == ObjectKind.ENUM
    assert nestedEnum5.name == "NestedEnum5"
    assert nestedEnum5.qualified_name == "ns5::Class5::NestedEnum5"
    assert nestedEnum5.values[0].name == "VALUE1"
    assert nestedEnum5.values[0].qualified_name == "ns5::Class5::NestedEnum5::VALUE1"

    nestedWeakEnum5 = doxygen.collect("ns5::Class5::NestedWeakEnum5")
    assert nestedWeakEnum5.kind == ObjectKind.ENUM
    assert nestedWeakEnum5.name == "NestedWeakEnum5"
    assert nestedWeakEnum5.qualified_name == "ns5::Class5::NestedWeakEnum5"
    assert nestedWeakEnum5.values[0].name == "VALUE"
    assert nestedWeakEnum5.values[0].qualified_name == "ns5::Class5::VALUE"


def test_var_with_enumerated_list(doxygen):
    var = doxygen.collect("var_with_enumerated_list")
    desc = var.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionList(
                title=None,
                contents=[
                    DescriptionParagraph(contents=[DescriptionText(contents="Item 1")]),
                    DescriptionParagraph(contents=[DescriptionText(contents="Item 2")]),
                ],
                ordered=True,
            )
        ]
    )
    assert desc == expected


def test_var_with_see(doxygen):
    var = doxygen.collect("var_with_see")
    desc = var.description

    print(desc)
    expected = DescriptionParagraph(
        contents=[
            DescriptionParagraph(
                contents=[
                    DescriptionText(contents="See "),
                    DescriptionText(
                        contents="var_with_enumerated_list which is a variable"
                    ),
                ]
            )
        ]
    )
    assert desc == expected


def test_overloaded_func5(doxygen):
    overloaded_func5 = doxygen.collect("ns5::overloaded_func5")
    assert overloaded_func5.kind == ObjectKind.OVERLOAD_SET
    assert len(overloaded_func5.overloads) == 2
    assert overloaded_func5.overloads[0].kind == ObjectKind.FUNCTION
    assert overloaded_func5.overloads[0].name == "overloaded_func5"
    assert overloaded_func5.overloads[0].type == "void"
    assert overloaded_func5.overloads[1].kind == ObjectKind.FUNCTION
    assert overloaded_func5.overloads[1].name == "overloaded_func5"
    assert overloaded_func5.overloads[1].type == "int"


def test_template_func5(doxygen):
    template_func5 = doxygen.collect("ns5::template_func5")
    template_func5.location = None

    print(template_func5)
    expected = Function(
        type="void",
        name="template_func5",
        qualified_name="ns5::template_func5",
        template_parameters=[Parameter(type="typename T", name=None)],
        parameters=[Parameter(type="T", name="t")],
        description=None,
        location=None,
    )
    assert template_func5 == expected


def test_template_func5_specialization(doxygen):
    template_func5 = doxygen.collect("ns5::template_func5<int>")
    template_func5.location = None

    print(template_func5)
    expected = Function(
        type="void",
        name="template_func5<int>",
        qualified_name="ns5::template_func5<int>",
        template_parameters=[],
        parameters=[Parameter(type="int", name="t")],
        description=None,
        location=None,
    )
    assert template_func5 == expected


def test_template_class5(doxygen):
    template_class5 = doxygen.collect("ns5::TemplateClass5")
    template_class5.location = None

    print(template_class5)
    expected = Compound(
        type=CompoundType.CLASS,
        qualified_name="ns5::TemplateClass5",
        title="ns5::TemplateClass5",
        template_parameters=[
            Parameter(type="typename T", name=None),
            Parameter(type="typename U", name=None),
        ],
        members=[],
        description=None,
        location=None,
    )
    assert template_class5 == expected


def test_template_class5_specialization(doxygen):
    template_class5 = doxygen.collect("ns5::TemplateClass5<T, int>")
    template_class5.location = None

    print(template_class5)
    expected = Compound(
        type=CompoundType.CLASS,
        qualified_name="ns5::TemplateClass5<T, int>",
        title="ns5::TemplateClass5<T, int>",
        template_parameters=[Parameter(type="typename T", name=None)],
        members=[],
        description=None,
        location=None,
    )
    assert template_class5 == expected


def test_func_that_throws(doxygen):
    func_that_throws = doxygen.collect("ns5::func_that_throws")
    func_that_throws.location = None

    print(func_that_throws)
    expected = Function(
        type="void",
        name="func_that_throws",
        qualified_name="ns5::func_that_throws",
        template_parameters=None,
        parameters=[Parameter(type="int", name="x")],
        description=DescriptionParagraph(
            contents=[
                DescriptionList(
                    title="Parameters",
                    contents=[
                        DescriptionParameter(
                            name="x",
                            description=DescriptionParagraph(
                                contents=[DescriptionText(contents="an int")]
                            ),
                            type="int",
                            direction=None,
                        )
                    ],
                    ordered=False,
                ),
                DescriptionList(
                    title="Exceptions",
                    contents=[
                        DescriptionParameter(
                            name="std::runtime_error",
                            description=DescriptionParagraph(
                                contents=[DescriptionText(contents="if x is negative")]
                            ),
                            type=None,
                            direction=None,
                        )
                    ],
                    ordered=False,
                ),
            ]
        ),
        location=None,
    )
    assert func_that_throws == expected
