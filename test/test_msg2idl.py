"""
@file test_msg2idl.py
@brief tools/msg2idl.py 測試：msg/ 每個欄位都必須依序出現在產生的 IDL 中
@author Taiphoon GCS Team
@date 2026-10-12
"""

import re
import sys
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

import msg2idl  # noqa: E402

MSG_DIR = PROJECT_ROOT / "msg" / "msg"


def load_definitions():
    """@brief 解析 msg/ 下所有介面"""
    return [msg2idl.parse_msg(path.stem, path.read_text(encoding="utf-8")) for path in sorted(MSG_DIR.glob("*.msg"))]


def struct_body(idl: str, name: str) -> str:
    """@brief 取出指定 struct 的內容"""
    match = re.search(r"struct %s_ \{\n(.*?)\n\};" % re.escape(name), idl, re.S)
    if match is None:
        raise AssertionError(f"IDL 缺少 struct {name}_")
    return match.group(1)


class Msg2IdlTest(unittest.TestCase):
    def test_every_field_in_order(self):
        definitions = load_definitions()
        self.assertTrue(definitions, "msg/msg/ 沒有介面檔案")
        idl = msg2idl.generate_idl("taiphoon_agent", definitions, [])
        for definition in definitions:
            lines = [line.strip() for line in struct_body(idl, definition.name).splitlines()]
            self.assertEqual(len(lines), len(definition.fields), definition.name)
            for line, msg_field in zip(lines, definition.fields):
                self.assertRegex(line, r"\b%s_?(\[\d+\])?;$" % re.escape(msg_field.name), definition.name)

    def test_dependencies_declared_first(self):
        idl = msg2idl.generate_idl("taiphoon_agent", load_definitions(), [])
        self.assertLess(idl.index("struct Waypoint_ "), idl.index("struct WaypointSequence_ "))

    def test_type_mapping(self):
        definition = msg2idl.parse_msg(
            "Sample",
            "builtin_interfaces/Time stamp\nfloat64[3] position\nfloat64[] values\nuint8[<=4] small\n"
            "string<=8 name\nuint8 MODE_A=1  # 常數\nuint32 sequence\n",
        )
        idl = msg2idl.generate_idl("pkg", [definition], [])
        body = struct_body(idl, "Sample")
        self.assertIn("builtin_interfaces::msg::dds_::Time_ stamp;", body)
        self.assertIn("double position[3];", body)
        self.assertIn("sequence<double> values;", body)
        self.assertIn("sequence<octet, 4> small;", body)
        self.assertIn("string<8> name;", body)
        self.assertIn("unsigned long sequence_;", body)
        self.assertIn("const octet MODE_A = 1;", idl)

    def test_rejects_unknown_external_type(self):
        definition = msg2idl.parse_msg("Bad", "geometry_msgs/Point point\n")
        with self.assertRaises(msg2idl.MsgError):
            msg2idl.generate_idl("pkg", [definition], [])

    def test_rejects_string_default(self):
        with self.assertRaises(msg2idl.MsgError):
            msg2idl.parse_msg("Bad", 'string name "abc"\n')


if __name__ == "__main__":
    unittest.main()
