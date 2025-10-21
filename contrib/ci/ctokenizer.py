#!/usr/bin/python3
#
# Copyright 2025 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# pylint: disable=missing-module-docstring,missing-class-docstring,
# pylint: disable=missing-function-docstring,too-few-public-methods,consider-using-enumerate

from typing import Optional
from enum import Enum
from fnmatch import fnmatch
import copy


class TokenHint(Enum):

    COMMENT = 1
    STRING = 2
    DEFINE = 3
    ENUM = 4
    LBRACKET = 5
    RBRACKET = 6
    FUNCTION = 7
    MACRO = 8
    INTEGER = 9
    STRUCT = 10

    @classmethod
    def value_of(cls, value):
        for k, v in cls.__members__.items():
            if k == value:
                return v
        raise ValueError(f"'{cls.__name__}' enum not found for '{value}'")


def _check_int(s: str) -> bool:
    if s.startswith("-"):
        s = s[1:]
    if s.startswith("0x"):
        s = s[2:]
    return s.isdigit()


class Token:

    def __init__(self, data: str, linecnt: int = 0, hint: Optional[TokenHint] = None):

        self.linecnt: int = linecnt
        self.linecnt_end: int = linecnt
        self.data: str = data
        self.hint: Optional[TokenHint] = hint

        # autohint
        if not self.hint:
            if data.startswith('"') and data.endswith('"'):
                self.hint = TokenHint.STRING
            elif data.startswith("/*") and data.endswith("*/"):
                self.hint = TokenHint.COMMENT
            elif data == "(":
                self.hint = TokenHint.LBRACKET
            elif data == ")":
                self.hint = TokenHint.RBRACKET
            elif _check_int(data):
                self.hint = TokenHint.INTEGER

    def __repr__(self) -> str:

        tmp = [f"linecnt={self.linecnt}"]
        if self.linecnt_end != self.linecnt:
            tmp.append(f"linecnt_end={self.linecnt_end}")
        if self.data:
            tmp.append(f"data='{self.data}'")
        if self.hint:
            tmp.append(f"hint={self.hint}")
        return f"Token({', '.join(tmp)})"


def _token_fuzzy_match(token: Token, data: str) -> bool:
    """data is of the form:
    * `literal`
    * `~fnmatch`
    * `@HINT`
    * `literal@HINT`
    * `~fnmatch@HINT`
    """

    # hint
    query: list[str] = data.split("@")
    try:
        if token.hint != TokenHint.value_of(query[1]):
            return False
    except IndexError:
        pass

    # fnmatch
    if query[0].startswith("~"):
        query_fnmatch = query[0][1:]
        if not query_fnmatch or query_fnmatch == "*":
            return True
        return fnmatch(token.data, query_fnmatch)

    # literal
    if query[0]:
        return token.data == query[0]

    # empty
    return True


class TokenList(list):

    def __init__(self, tokens: Optional[list[Token]] = None):

        for token in tokens or []:
            self.append(token)

    def append(self, token: Token) -> None:

        # autohint
        if not token.hint and token.data != ";":
            if len(self) >= 1 and self[-1].data == "#define":
                token.hint = TokenHint.DEFINE
            elif len(self) >= 1 and self[-1].data == "enum":
                token.hint = TokenHint.ENUM
            elif len(self) >= 1 and self[-1].data == "struct":
                token.hint = TokenHint.STRUCT

        # autohint previous token
        if len(self) > 0 and token.hint == TokenHint.LBRACKET:
            token_prev: Token = self[-1]
            if not token_prev.hint:
                if token_prev.data.find("_") != -1:
                    if token_prev.data.upper() == token_prev.data:
                        token_prev.hint = TokenHint.MACRO
                    else:
                        token_prev.hint = TokenHint.FUNCTION

        # add
        list.append(self, token)

    def find_fuzzy(
        self,
        data_fuzzy: list[str],
        offset: Optional[int] = None,
        reverse: bool = False,
        skip_comments: bool = False,
    ) -> int:
        """
        Look for a fuzzy token sequence and return the position to the
        first token. Returns -1 if not found.
        """
        if reverse:
            pos_range = range(offset or len(self) - 1, 0, -1)
        else:
            pos_range = range(offset or 0, len(self) - (len(data_fuzzy) - 1))
        for pos in pos_range:

            all_match: bool = True
            for datapos in range(len(data_fuzzy)):
                comment_offset: int = 0
                if skip_comments and self[pos + datapos].hint == TokenHint.COMMENT:
                    comment_offset = 1
                token = self[pos + datapos + comment_offset]
                if not _token_fuzzy_match(token, data_fuzzy[datapos]):
                    all_match = False
                    break
            if all_match:
                return pos
        return -1

    def endswith_fuzzy(self, data_fuzzy: list[str]) -> bool:
        """
        Look for a fuzzy token sequence at the end of the list.
        Returns False if not found.
        """
        offset = len(self) - len(data_fuzzy)
        if offset < 0:
            return False
        for datapos in range(len(data_fuzzy)):
            token = self[offset + datapos]
            if not _token_fuzzy_match(token, data_fuzzy[datapos]):
                return False
        return True

    def count_fuzzy(self, data_fuzzy: list[str]) -> int:
        """
        Return the number of fuzzy matches matching all tokens.
        """
        cnt: int = 0
        for pos in range(len(self) - (len(data_fuzzy) - 1)):
            all_match: bool = True
            for datapos in range(len(data_fuzzy)):
                if not _token_fuzzy_match(self[pos + datapos], data_fuzzy[datapos]):
                    all_match = False
                    break
            if all_match:
                cnt += 1
        return cnt


class NodeHint(Enum):

    UNION = 1
    ENUM = 2
    STRUCT = 3
    STRUCT_TYPEDEF = 4
    STRUCT_CONST = 5


class Node:

    def __init__(
        self, depth: int, linecnt: int, tokens_pre: Optional[TokenList] = None
    ):
        self.depth: int = depth
        self.linecnt: int = linecnt
        self.linecnt_end: int = linecnt
        self.tokens_pre: TokenList = tokens_pre or TokenList()
        self.tokens: TokenList = TokenList()
        self.hint: Optional[NodeHint] = None

    def __repr__(self) -> str:

        tmp = [f"depth={self.depth}", f"linecnt={self.linecnt}"]
        if self.linecnt_end != self.linecnt:
            tmp.append(f"linecnt_end={self.linecnt_end}")
        if self.hint:
            tmp.append(f"hint={self.hint}")
        if self.tokens_pre:
            tmp.append(f"tokens_pre={[token.data for token in self.tokens_pre]}")
        if self.tokens:
            tmp.append(f"tokens={[token.data for token in self.tokens]}")
        return f"Node({', '.join(tmp)})"


class Tokenizer:
    def __init__(self, data: str):
        self.tokens: TokenList = TokenList()
        self._nodes: list[Node] = []
        self._acc: str = ""
        self._linecnt: int = 1
        if data:
            self._parse(data)

    def _add_token(self, token: Token) -> None:

        # merge into previous token
        if self.tokens:
            token_prev: Token = self.tokens[-1]

            # STRING -> STRING
            if token_prev.hint == TokenHint.STRING and token.hint == TokenHint.STRING:
                new = token_prev.data[:-1] + token.data[1:]
                token_prev.data = new
                token_prev.linecnt_end = token.linecnt
                return

            # COMMENT -> COMMENT
            if token_prev.hint == TokenHint.COMMENT and token.hint == TokenHint.COMMENT:
                new = token_prev.data + " " + token.data
                token_prev.data = new
                token_prev.linecnt_end = token.linecnt
                return

            # "-" -> INTEGER
            if token_prev.data == "-" and token.hint == TokenHint.INTEGER:
                new = token_prev.data + token.data
                token_prev.data = new
                return

        # add new
        self.tokens.append(token)

    def dump_acc(self, hint: Optional[TokenHint] = None) -> None:

        stripped = self._acc.strip()
        if stripped:
            if fnmatch(stripped, "G_G*_FORMAT"):
                stripped = '"%u"'
            self._add_token(Token(stripped, linecnt=self._linecnt, hint=hint))
        self._acc = ""

    def _parse(self, data: str):

        is_quote_mode: bool = False
        is_comment_mode: bool = False
        is_escape_mode: bool = False

        for pos, char in enumerate(data):

            # newline
            if char == "\n":

                if is_comment_mode:
                    self.dump_acc(hint=TokenHint.COMMENT)
                else:
                    self.dump_acc()
                self._linecnt += 1
                continue

            # only valid once
            is_escape_mode = data[pos - 1] == "\\" and data[pos - 2] != "\\"

            # string quotes
            if (
                char == '"'
                and not is_escape_mode
                and not (data[pos - 1] == "'" and data[pos + 1] == "'")
            ):
                is_quote_mode = not is_quote_mode

            # delimiter
            dump_char: bool = False
            if (
                char
                in [
                    " ",
                    "*",
                    "(",
                    ")",
                    "{",
                    "}",
                    ",",
                    "&",
                    ";",
                    ">",
                    "<",
                    "-",
                    "[",
                    "]",
                    "!",
                ]
                and not is_comment_mode
                and not is_quote_mode
            ):
                self.dump_acc()
                dump_char = True

            # comment
            if data[pos : pos + 2] == "/*" and not is_quote_mode:
                is_comment_mode = True

            self._acc += char

            # end comment
            if data[pos - 1 : pos + 1] == "*/" and not is_quote_mode:
                self.dump_acc(hint=TokenHint.COMMENT)
                is_comment_mode = False

            # delimiter
            if dump_char:
                self.dump_acc()

        # any left
        self.dump_acc()

    def _add_node(self, node: Node):

        # auto-hint node
        if not node.hint:
            try:
                if node.tokens_pre[-1].data == "union":
                    node.hint = NodeHint.UNION
                elif node.tokens_pre[-1].data == "enum":
                    node.hint = NodeHint.ENUM
                elif node.tokens_pre[-1].data == "struct":
                    node.hint = NodeHint.STRUCT
                    if node.tokens_pre[-2].data == "typedef":
                        node.hint = NodeHint.STRUCT_TYPEDEF
                    elif node.tokens_pre[-2].data == "const":
                        node.hint = NodeHint.STRUCT_CONST
            except IndexError:
                pass

        # add
        self._nodes.append(node)

    def _ensure_nodes(self) -> None:

        tokens_acc: TokenList = TokenList()
        depth: int = 0
        stack: list[Node] = []
        node_parent: Optional[Node] = None

        for token in self.tokens:

            # start, end or continue
            if token.data == "{":
                if stack:
                    stack[-1].tokens.extend(tokens_acc)
                node = Node(
                    depth=len(stack),
                    linecnt=token.linecnt,
                    tokens_pre=copy.copy(tokens_acc),
                )
                stack.append(node)
                self._add_node(node)
                tokens_acc.clear()
                depth += 1
            elif token.data == "}":
                node_parent = stack.pop()
                node_parent.tokens.extend(tokens_acc)
                node_parent.linecnt_end = token.linecnt
                tokens_acc.clear()
                depth -= 1
            else:
                # reposition typedef struct names to be before the {
                if (
                    node_parent
                    and node_parent.tokens_pre
                    and node_parent.tokens_pre[-1].data
                    in [
                        "enum",
                        "struct",
                        "union",
                    ]
                ):
                    node_parent.tokens_pre.append(token)
                else:
                    tokens_acc.append(token)

        # create a fake node
        if not self._nodes and tokens_acc:
            self._add_node(
                Node(depth=0, linecnt=tokens_acc[0].linecnt, tokens_pre=tokens_acc)
            )

        # sanity check
        if depth != 0:
            raise ValueError("has unequal nesting")

    @property
    def nodes(self) -> list[Node]:

        if not self._nodes:
            self._ensure_nodes()
        return self._nodes
