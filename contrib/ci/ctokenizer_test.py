#!/usr/bin/python3
#
# Copyright 2025 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# pylint: disable=missing-module-docstring,missing-class-docstring,missing-function-docstring

import unittest

from ctokenizer import Tokenizer, Token, TokenList


class TestCTokenize(unittest.TestCase):

    def _compare_tokens(self, data: str, tokens_wanted: list[str]) -> None:

        tokens = Tokenizer(data).tokens
        print(tokens)
        tokens_data = [token.data for token in tokens]
        self.assertEqual(tokens_data, tokens_wanted)

    def _compare_tokens_full(self, data: str, tokens_wanted: list[str]) -> None:

        tokens = Tokenizer(data).tokens
        print(tokens)
        tokens_data = [str(token) for token in tokens]
        self.assertEqual(tokens_data, tokens_wanted)

    def _compare_nodes(self, data: str, nodes_wanted: list[str]) -> None:

        tokenizer = Tokenizer(data)
        nodes = tokenizer.nodes
        print(nodes)
        nodes_data = [str(node) for node in tokenizer.nodes]
        self.assertEqual(nodes_data, nodes_wanted)

    def test_tokens(self):

        self._compare_tokens_full(
            " /* comment */ /* comment2 */ ",
            [
                "Token(linecnt=1, data='/* comment */ /* comment2 */', hint=TokenHint.COMMENT)",
            ],
        )
        self._compare_tokens_full(
            "/* one\n\ttwo. */",
            [
                "Token(linecnt=1, linecnt_end=2, data='/* one two. */', hint=TokenHint.COMMENT)",
            ],
        )
        self._compare_tokens_full(
            "/**\n * comment3\n * comment4\n **/",
            [
                "Token(linecnt=1, linecnt_end=4, data='/** * comment3 * comment4 **/', hint=TokenHint.COMMENT)",
            ],
        )
        self._compare_tokens_full(
            "/**\n * comment3\n * comment4\n **/",
            [
                "Token(linecnt=1, linecnt_end=4, data='/** * comment3 * comment4 **/', hint=TokenHint.COMMENT)",
            ],
        )
        self._compare_tokens(
            "guint8 *foo_cb(guint16 *buf)\n{\nreturn 42;\n}\n",
            [
                "guint8",
                "*",
                "foo_cb",
                "(",
                "guint16",
                "*",
                "buf",
                ")",
                "{",
                "return",
                "42",
                ";",
                "}",
            ],
        )
        self._compare_tokens(
            'g_print("hello world");',
            [
                "g_print",
                "(",
                '"hello world"',
                ")",
                ";",
            ],
        )
        self._compare_tokens(
            '1 \\"2\\" 3',
            [
                "1",
                '\\"2\\"',
                "3",
            ],
        )
        self._compare_tokens(
            "if (&char == '\"')",
            [
                "if",
                "(",
                "&",
                "char",
                "==",
                "'\"'",
                ")",
            ],
        )
        self._compare_tokens(
            'xb_node_query_first("component/*");',
            [
                "xb_node_query_first",
                "(",
                '"component/*"',
                ")",
                ";",
            ],
        )
        self._compare_tokens(
            "g_set_error_literal(error,\n\tFWUPD_ERROR,\n\tFWUPD_ERROR_NOTHING_TO_DO,\n\t"
            '"no" G_GSIZE_FORMAT "pe "\n\t" no"\n\t"pe2");\n',
            [
                "g_set_error_literal",
                "(",
                "error",
                ",",
                "FWUPD_ERROR",
                ",",
                "FWUPD_ERROR_NOTHING_TO_DO",
                ",",
                '"no%upe  nope2"',
                ")",
                ";",
            ],
        )
        self._compare_tokens(
            "return -1;\n",
            [
                "return",
                "-1",
                ";",
            ],
        )
        self._compare_tokens(
            "#define FOO_001 0x123\n",
            [
                "#define",
                "FOO_001",
                "0x123",
            ],
        )
        self._compare_tokens(
            'g_string_append(str, "\\"");',
            [
                "g_string_append",
                "(",
                "str",
                ",",
                '"\\""',
                ")",
                ";",
            ],
        )

    def test_token_list(self):

        toklist = TokenList(
            [
                Token("/*start*/"),
                Token("guint8"),
                Token("buf"),
                Token("["),
                Token("10"),
                Token("]"),
                Token(";"),
                Token("/*end*/"),
            ]
        )
        print(toklist)
        self.assertEqual(toklist.find_fuzzy(["~guint*", "~*", "[", "~*", "]", ";"]), 1)
        self.assertTrue(toklist.endswith_fuzzy([";", "/*end*/"]))
        self.assertFalse(toklist.endswith_fuzzy(["]", ";"]))

        toklist = TokenList(
            [
                Token("one"),
            ]
        )
        print(toklist)
        self.assertFalse(toklist.endswith_fuzzy(["one", "two"]))

        toklist = TokenList(
            [
                Token("match1"),  # 0
                Token("token"),  # 1
                Token("match2"),  # 2
                Token("token"),  # 3
                Token("match3"),  # 4
            ]
        )
        print(toklist)
        self.assertEqual(toklist.find_fuzzy(["~match*"]), 0)
        self.assertEqual(toklist.find_fuzzy(["~match*"], reverse=True), 4)
        self.assertEqual(toklist.find_fuzzy(["~match*"], offset=1), 2)
        self.assertEqual(toklist.find_fuzzy(["~match*"], offset=3, reverse=True), 2)

        toklist = TokenList(
            [
                Token("/*start*/"),
                Token("guint8"),
                Token("buf"),
                Token("["),
                Token("10"),
                Token("]"),
                Token(";"),
                Token("/*end*/"),
            ]
        )
        print(toklist)
        self.assertEqual(toklist.find_fuzzy(["/*end*/", "DOESNOTEXIST"]), -1)

        toklist = TokenList(
            [
                Token("fu_memread_uint16"),
                Token("fu_memread_uint32"),
                Token("fu_memread_uint64"),
            ]
        )
        print(toklist)
        self.assertEqual(toklist.count_fuzzy(["~fu_memwrite_uint*"]), 0)
        self.assertEqual(toklist.count_fuzzy(["~fu_memread_uint*"]), 3)

        toklist = TokenList(
            [
                Token("/* comment1 */"),
                Token("func1_cb"),
                Token("/* comment2 */"),
                Token("func2_cb"),
            ]
        )
        print(toklist)
        self.assertEqual(
            toklist.find_fuzzy(["func1_cb", "func2_cb"], skip_comments=True), 1
        )

        # autohint
        toklist = TokenList(
            [
                Token("func_cb"),
                Token("("),
                Token("-1"),
                Token(")"),
            ]
        )
        print(toklist)
        self.assertEqual(
            str(toklist[0]), "Token(linecnt=0, data='func_cb', hint=TokenHint.FUNCTION)"
        )
        self.assertEqual(
            str(toklist[1]), "Token(linecnt=0, data='(', hint=TokenHint.LBRACKET)"
        )
        self.assertEqual(
            str(toklist[2]), "Token(linecnt=0, data='-1', hint=TokenHint.INTEGER)"
        )
        self.assertEqual(
            str(toklist[3]), "Token(linecnt=0, data=')', hint=TokenHint.RBRACKET)"
        )
        self.assertEqual(
            toklist.find_fuzzy(["func_cb@MACRO", "("], skip_comments=True), -1
        )
        self.assertEqual(
            toklist.find_fuzzy(["func_cb@FUNCTION", "("], skip_comments=True), 0
        )
        self.assertEqual(toklist.find_fuzzy(["@FUNCTION", "("], skip_comments=True), 0)
        self.assertEqual(toklist.find_fuzzy(["", "("], skip_comments=True), 0)

    def test_nodes(self):

        self._compare_nodes(
            "typedef struct {\n\tgchar *id;\n} FwupdDevicePrivate;\n",
            [
                "Node(depth=0, linecnt=1, linecnt_end=3, hint=NodeHint.STRUCT_TYPEDEF, "
                "tokens_pre=['typedef', 'struct', 'FwupdDevicePrivate'], "
                "tokens=['gchar', '*', 'id', ';'])",
            ],
        )

        self._compare_nodes(
            "#define FOO_001 0x123\n#define FOO_002 0x123\n",
            [
                "Node(depth=0, linecnt=1, tokens_pre=['#define', 'FOO_001', '0x123', "
                "'#define', 'FOO_002', '0x123'])",
            ],
        )

        self._compare_nodes(
            "union { guint8 one; guint two; } name;",
            [
                "Node(depth=0, linecnt=1, hint=NodeHint.UNION, tokens_pre=['union', 'name'], "
                "tokens=['guint8', 'one', ';', 'guint', 'two', ';'])"
            ],
        )
        self._compare_nodes(
            "void main (void) {"
            "  gint rc;"
            "  if (1) {"
            "    cond1;"
            "  }"
            "  middle;"
            "  if (1) {"
            "    cond2;"
            "  }"
            "  return rc;"
            "}",
            [
                "Node(depth=0, linecnt=1, tokens_pre=['void', 'main', '(', 'void', ')'], "
                "tokens=['gint', 'rc', ';', 'if', '(', '1', ')', 'middle', ';', 'if', "
                "'(', '1', ')', 'return', 'rc', ';'])",
                "Node(depth=1, linecnt=1, tokens_pre=['gint', 'rc', ';', 'if', '(', '1', "
                "')'], tokens=['cond1', ';'])",
                "Node(depth=1, linecnt=1, tokens_pre=['middle', ';', 'if', '(', '1', ')'], "
                "tokens=['cond2', ';'])",
            ],
        )


if __name__ == "__main__":
    unittest.main()
