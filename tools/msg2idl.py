#!/usr/bin/env python3
"""
@file msg2idl.py
@brief 將 ROS2 .msg 介面轉為 CycloneDDS idlc 可編譯的 IDL
@author Taiphoon GCS Team
@date 2026-10-12

產生的型別名稱與 ROS2 Humble rmw_cyclonedds_cpp 在網路上使用的名稱相同，
例如 taiphoon_agent/msg/AgentState → taiphoon_agent::msg::dds_::AgentState_。
欄位名稱不會出現在網路上（rmw_cyclonedds 不傳送型別資訊），只有欄位順序與型別
決定 CDR 格式，因此與 IDL 關鍵字衝突的欄位名稱可以安全地加上底線。

不支援的語法一律報錯，避免介面更新後靜默產生錯誤的型別。
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

# ROS2 基本型別 → IDL 型別。uint8／char／byte 使用 octet，避免依賴 IDL 4 的 int8 擴充語法。
PRIMITIVE_TYPES = {
    "bool": "boolean",
    "byte": "octet",
    "char": "octet",
    "uint8": "octet",
    "int8": "int8",
    "int16": "short",
    "uint16": "unsigned short",
    "int32": "long",
    "uint32": "unsigned long",
    "int64": "long long",
    "uint64": "unsigned long long",
    "float32": "float",
    "float64": "double",
    "string": "string",
}

# 本專案以外、由 ros_common.idl 提供的型別
EXTERNAL_TYPES = {
    "builtin_interfaces/Time": "builtin_interfaces::msg::dds_::Time_",
}

# IDL 關鍵字（比對時不分大小寫，IDL 規定識別字不得與關鍵字僅有大小寫差異）
IDL_KEYWORDS = {
    "abstract", "any", "alias", "attribute", "bitfield", "bitmask", "bitset", "boolean", "case", "char",
    "component", "connector", "const", "consumes", "context", "custom", "default", "double", "exception",
    "emits", "enum", "eventtype", "factory", "false", "finder", "fixed", "float", "getraises", "getter",
    "home", "import", "in", "inout", "interface", "local", "long", "manages", "map", "mirrorport",
    "module", "multiple", "native", "object", "octet", "oneway", "out", "primarykey", "private", "port",
    "porttype", "provides", "public", "publishes", "raises", "readonly", "setraises", "setter",
    "sequence", "short", "string", "struct", "supports", "switch", "true", "truncatable", "typedef",
    "typeid", "typename", "typeprefix", "unsigned", "union", "uses", "valuebase", "valuetype", "void",
    "wchar", "wstring", "int8", "uint8", "int16", "int32", "int64", "uint16", "uint32", "uint64",
}

FIELD_PATTERN = re.compile(r"^(?P<type>\S+)\s+(?P<name>[A-Za-z][A-Za-z0-9_]*)(?:\s+(?P<default>.+))?$")
CONSTANT_PATTERN = re.compile(r"^(?P<type>\S+)\s+(?P<name>[A-Z][A-Z0-9_]*)\s*=\s*(?P<value>\S+)$")
TYPE_PATTERN = re.compile(
    r"^(?P<base>[A-Za-z][A-Za-z0-9_/]*)(?:<=(?P<string_bound>\d+))?"
    r"(?:\[(?P<array>(?:<=)?\d*)\])?$"
)


class MsgError(Exception):
    """@brief .msg 內容無法轉換時拋出"""


@dataclass
class MsgField:
    """@brief .msg 中的一個欄位"""
    base_type: str
    name: str
    string_bound: Optional[int] = None
    array_size: Optional[int] = None
    sequence_bound: Optional[int] = None
    is_sequence: bool = False


@dataclass
class MsgConstant:
    """@brief .msg 中的一個常數"""
    base_type: str
    name: str
    value: str


@dataclass
class MsgDefinition:
    """@brief 單一 .msg 檔的解析結果"""
    name: str
    fields: List[MsgField] = field(default_factory=list)
    constants: List[MsgConstant] = field(default_factory=list)


def strip_comment(line: str) -> str:
    """
    @brief 移除行尾註解
    @param line 原始行
    @return 去除註解與前後空白後的內容
    """
    if '"' in line or "'" in line:
        raise MsgError(f"不支援含字串預設值的欄位：{line.strip()}")
    return line.split("#", 1)[0].strip()


def parse_type(type_text: str) -> MsgField:
    """
    @brief 解析欄位型別（含陣列與字串長度限制）
    @param type_text 型別字串，例如 float32[3]、float64[]、string<=10
    @return 只填入型別資訊的 MsgField
    """
    match = TYPE_PATTERN.match(type_text)
    if match is None:
        raise MsgError(f"無法解析型別：{type_text}")

    result = MsgField(base_type=match.group("base"), name="")
    if match.group("string_bound") is not None:
        if result.base_type != "string":
            raise MsgError(f"只有 string 可以設定長度上限：{type_text}")
        result.string_bound = int(match.group("string_bound"))

    array = match.group("array")
    if array is not None:
        if array == "":
            result.is_sequence = True
        elif array.startswith("<="):
            result.is_sequence = True
            result.sequence_bound = int(array[2:])
        else:
            result.array_size = int(array)
    return result


def parse_msg(name: str, text: str) -> MsgDefinition:
    """
    @brief 解析 .msg 內容
    @param name 訊息名稱（檔名，不含副檔名）
    @param text .msg 檔內容
    @return 解析結果
    """
    definition = MsgDefinition(name=name)
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = strip_comment(raw_line)
        if not line:
            continue

        constant = CONSTANT_PATTERN.match(line)
        if constant is not None:
            if constant.group("type") not in PRIMITIVE_TYPES or constant.group("type") == "string":
                raise MsgError(f"{name}.msg:{line_number} 常數只支援數值型別")
            definition.constants.append(
                MsgConstant(constant.group("type"), constant.group("name"), constant.group("value")))
            continue

        match = FIELD_PATTERN.match(line)
        if match is None:
            raise MsgError(f"{name}.msg:{line_number} 無法解析：{line}")
        parsed = parse_type(match.group("type"))
        parsed.name = match.group("name")
        definition.fields.append(parsed)
    return definition


def resolve_type(base_type: str, package: str, known: Dict[str, MsgDefinition]) -> str:
    """
    @brief 將 .msg 型別名稱轉為 IDL 型別名稱
    @param base_type .msg 中的型別（不含陣列）
    @param package 目前套件名稱
    @param known 本套件所有訊息
    @return IDL 型別名稱
    """
    if base_type in PRIMITIVE_TYPES:
        return PRIMITIVE_TYPES[base_type]
    if base_type in EXTERNAL_TYPES:
        return EXTERNAL_TYPES[base_type]

    short_name = base_type
    if "/" in base_type:
        type_package, short_name = base_type.split("/", 1)
        if type_package != package:
            raise MsgError(f"不支援的外部型別：{base_type}")
    if short_name not in known:
        raise MsgError(f"找不到型別：{base_type}")
    return f"{package}::msg::dds_::{short_name}_"


def local_dependencies(definition: MsgDefinition, package: str, known: Dict[str, MsgDefinition]) -> List[str]:
    """
    @brief 列出訊息直接依賴的本套件訊息
    @return 依賴的訊息名稱
    """
    result = []
    for msg_field in definition.fields:
        short_name = msg_field.base_type.split("/", 1)[-1]
        is_local = msg_field.base_type not in PRIMITIVE_TYPES and msg_field.base_type not in EXTERNAL_TYPES
        if is_local and short_name in known:
            result.append(short_name)
    return result


def sort_by_dependency(package: str, known: Dict[str, MsgDefinition]) -> List[MsgDefinition]:
    """
    @brief 依相依關係排序，IDL 要求型別先宣告後使用
    @return 排序後的訊息
    """
    ordered: List[MsgDefinition] = []
    visiting = set()
    done = set()

    def visit(name: str) -> None:
        if name in done:
            return
        if name in visiting:
            raise MsgError(f"訊息存在循環相依：{name}")
        visiting.add(name)
        for dependency in local_dependencies(known[name], package, known):
            visit(dependency)
        visiting.discard(name)
        done.add(name)
        ordered.append(known[name])

    for name in sorted(known):
        visit(name)
    return ordered


def idl_identifier(name: str) -> str:
    """
    @brief 避開 IDL 關鍵字；欄位名稱不影響網路格式
    @param name .msg 欄位名稱
    @return 合法的 IDL 識別字
    """
    return f"{name}_" if name.lower() in IDL_KEYWORDS else name


def field_to_idl(msg_field: MsgField, package: str, known: Dict[str, MsgDefinition]) -> str:
    """
    @brief 將欄位轉為一行 IDL 宣告
    @return IDL 宣告（不含縮排）
    """
    idl_type = resolve_type(msg_field.base_type, package, known)
    if msg_field.string_bound is not None:
        idl_type = f"string<{msg_field.string_bound}>"

    name = idl_identifier(msg_field.name)
    if msg_field.is_sequence:
        bound = f", {msg_field.sequence_bound}" if msg_field.sequence_bound is not None else ""
        return f"sequence<{idl_type}{bound}> {name};"
    if msg_field.array_size is not None:
        return f"{idl_type} {name}[{msg_field.array_size}];"
    return f"{idl_type} {name};"


def generate_idl(package: str, definitions: List[MsgDefinition], sources: List[str]) -> str:
    """
    @brief 產生完整 IDL 檔內容
    @param package 套件名稱
    @param definitions 所有訊息
    @param sources 來源檔名（寫入檔頭供追溯）
    @return IDL 內容
    """
    known = {definition.name: definition for definition in definitions}
    lines = [
        "// 由 tools/msg2idl.py 自動產生，請勿手動修改。",
        f"// 來源：{', '.join(sources)}",
        '#include "ros_common.idl"',
        "",
        f"module {package} {{",
        "module msg {",
        "module dds_ {",
    ]
    for definition in sort_by_dependency(package, known):
        if definition.constants:
            lines.append(f"module {definition.name}_Constants {{")
            for constant in definition.constants:
                const_type = PRIMITIVE_TYPES[constant.base_type]
                lines.append(f"    const {const_type} {constant.name} = {constant.value};")
            lines.append("};")
        if not definition.fields:
            raise MsgError(f"{definition.name}.msg 沒有欄位")
        lines.append(f"struct {definition.name}_ {{")
        for msg_field in definition.fields:
            lines.append(f"    {field_to_idl(msg_field, package, known)}")
        lines.append("};")
    lines += ["};", "};", "};", ""]
    return "\n".join(lines)


def main(argv: List[str]) -> int:
    """
    @brief 命令列入口
    @param argv 命令列參數
    @return 程式結束碼
    """
    parser = argparse.ArgumentParser(description="將 ROS2 .msg 轉為 CycloneDDS IDL")
    parser.add_argument("--package", required=True, help="ROS2 套件名稱，例如 taiphoon_agent")
    parser.add_argument("--output", required=True, type=Path, help="輸出的 .idl 檔")
    parser.add_argument("msg_files", nargs="+", type=Path, help="輸入的 .msg 檔")
    args = parser.parse_args(argv)

    try:
        definitions = [parse_msg(path.stem, path.read_text(encoding="utf-8")) for path in args.msg_files]
        content = generate_idl(args.package, definitions, sorted(path.name for path in args.msg_files))
    except (MsgError, OSError) as error:
        print(f"msg2idl: {error}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
