#!/usr/bin/env python3
"""No third-party dependencies. This catches broken source/resource wiring early.

It is NOT a substitute for compiling Qt/C++ or loading the QML engine.
"""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
QML_DIR = ROOT / "src" / "qml"
issues = []


def check(condition: bool, message: str):
    if not condition:
        issues.append(message)


qrc = ET.parse(QML_DIR / "qml.qrc")
resources = {}
for resource in qrc.findall("qresource"):
    prefix = resource.get("prefix", "")
    for item in resource.findall("file"):
        candidate = (QML_DIR / str(item.text)).resolve()
        check(candidate.is_file(), f"resource missing: {item.text}")
        url = f"{prefix.rstrip('/')}/{item.text}"
        check(url not in resources, f"resource duplicated: {url}")
        resources[url] = candidate
check("/qml/main.qml" in resources, "missing qrc:/qml/main.qml")
check("/qml/ActionButton.qml" in resources, "missing custom ActionButton component")
# Verify all components in the Findex-based design system are packaged.
for component in ("FButton", "FIcon", "FTextField", "FTextArea", "FComboBox",
                  "FCheckBox", "FDialog", "FMenu", "FMenuItem", "FMenuSeparator",
                  "FToolTip", "FWindowButton", "FScrollBar", "FMenuTrigger", "FBusyIndicator"):
    check(f"/qml/ui/{component}.qml" in resources, f"missing design-system component: {component}")
check("/qml/theme/AppTheme.qml" in resources, "missing Findex theme singleton")
check("/qml/theme/qmldir" in resources, "missing theme qmldir")
for icon_name in ("folder", "zip", "description", "check", "download", "close",
                  "minimize", "search", "home", "preview", "filter-none", "expand-more"):
    for scheme in ("light", "dark"):
        check(f"/qml/assets/icons/{scheme}/{icon_name}.png" in resources,
              f"missing Findex icon resource {scheme}/{icon_name}")
check("/qtquickcontrols2.conf" in resources, "Qt Quick Controls configuration missing")

# Full icon inventory check, not just a dozen representative names.
for qml_file in QML_DIR.rglob("*.qml"):
    body = qml_file.read_text(encoding="utf-8")
    for icon_name in set(re.findall(r'\b(?:iconName|name)\s*:\s*["\']([a-z][a-z0-9-]*)["\']', body)):
        for variant in ("light", "dark"):
            check(f"/qml/assets/icons/{variant}/{icon_name}.png" in resources,
                  f"undeclared icon {variant}/{icon_name} used by {qml_file.relative_to(QML_DIR)}")

# No leftover default-styled controls should appear in the main workspace,
# and all custom components used by it must be part of qrc.
main_qml = (QML_DIR / "main.qml").read_text(encoding="utf-8")
for component in set(re.findall(r"\bUi\.(F\w+)\s*\{", main_qml)):
    check(f"/qml/ui/{component}.qml" in resources, f"UI control not bundled: {component}")
for raw_control in ("Button", "ComboBox", "CheckBox", "TextField", "TextArea", "ToolTip", "Menu", "MenuItem", "ScrollBar", "Dialog"):
    check(not re.search(r"(?<![\w.])" + raw_control + r"\s*\{", main_qml),
          f"unstyled Qt Quick Controls instance in main.qml: {raw_control}")

header = (ROOT / "src/app/archive_controller.hpp").read_text(encoding="utf-8")
qml = (QML_DIR / "main.qml").read_text(encoding="utf-8")
invokables = set(re.findall(r"Q_INVOKABLE\s+[\w:<>*&]+\s+(\w+)\s*\(", header))
properties = set(re.findall(r"Q_PROPERTY\([^)]*?\b(\w+)\s+READ\s+", header))
signals = set(re.findall(r"^\s+void\s+(\w+)\s*\(", header, re.M))
for member in set(re.findall(r"\bbackend\.(\w+)\b", qml)):
    check(member in (invokables | properties | signals),
          f"QML backend member not declared in ArchiveController: {member}")
for prop in ("status", "busy", "archiveName", "archivePath", "rows"):
    check(prop in properties, f"expected Q_PROPERTY missing: {prop}")

# Basic brace check while respecting QML quoted strings and JS comments.
def strip_literals(source: str) -> str:
    result = []
    state = "code"
    i = 0
    while i < len(source):
        c = source[i]
        following = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if c == "/" and following == "/":
                state = "line"; i += 2; continue
            if c == "/" and following == "*":
                state = "block"; i += 2; continue
            if c in ("'", '"', '`'):
                state = c
                result.append(" ")
            else:
                result.append(c)
        elif state == "line":
            if c == "\n":
                state = "code"; result.append("\n")
        elif state == "block":
            if c == "*" and following == "/":
                state = "code"; i += 2; continue
        else:
            if c == "\\":
                i += 1
            elif c == state:
                state = "code"
        i += 1
    return "".join(result)

for path in QML_DIR.rglob("*.qml"):
    source = path.read_text(encoding="utf-8")
    parsed = strip_literals(source)
    check(not re.search(r"\}\s*;\s*(?:[A-Z][A-Za-z0-9_]*|Ui\.[A-Z][A-Za-z0-9_]*)\s*\{", source),
          f"invalid semicolon between QML child objects in {path.relative_to(QML_DIR)}")
    level = 0
    for character in parsed:
        if character == "{":
            level += 1
        elif character == "}":
            level -= 1
            check(level >= 0, f"unbalanced closing bracket in {path.name}")
            if level < 0:
                level = 0
    check(level == 0, f"unbalanced braces in {path.name}")

# Qt Quick positioners compute their implicit size themselves. Writing
# implicitWidth/implicitHeight on a Flow/Row/Column/Grid instance can make
# QQmlApplicationEngine reject the entire main window (seen with Qt 6.9).
for path in QML_DIR.rglob("*.qml"):
    source = path.read_text(encoding="utf-8")
    parsed = strip_literals(source)
    check(not re.search(r"\}\s*;\s*(?:[A-Z][A-Za-z0-9_]*|Ui\.[A-Z][A-Za-z0-9_]*)\s*\{", source),
          f"invalid semicolon between QML child objects in {path.relative_to(QML_DIR)}")
    for match in re.finditer(r"\b(?:Flow|Row|Column|Grid)\s*\{", parsed):
        start = match.end()
        depth = 1
        i = start
        token = ""
        while i < len(parsed) and depth > 0:
            char = parsed[i]
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
            elif depth == 1:
                token += char
            i += 1
        check(not re.search(r"\bimplicit(?:Width|Height)\s*:", token),
              f"read-only implicit size assigned on Qt Quick positioner in {path.name} at character {match.start()}")

# QML allows semicolons between property assignments, but not between
# sibling object declarations. This caused the Windows startup failure in
# version 0.7.9 (three inline Dialog contentItem declarations).
for path in QML_DIR.rglob("*.qml"):
    source = path.read_text(encoding="utf-8")
    parsed = strip_literals(source)
    check(not re.search(r"\}\s*;\s*(?:[A-Z][A-Za-z0-9_]*|Ui\.[A-Z][A-Za-z0-9_]*)\s*\{", source),
          f"invalid semicolon between QML child objects in {path.relative_to(QML_DIR)}")
    for match in re.finditer(r"\}\s*;\s*[A-Z][A-Za-z0-9_]*\s*\{", parsed):
        line = parsed.count("\n", 0, match.start()) + 1
        check(False, f"semicolon between sibling QML objects in {path.name}:{line}")

    # Avoid ambiguous bindings where `border` can refer to the Rectangle's
    # grouped border property rather than the intended window-level color.
    check(not re.search(r"\bborder\.color\s*:\s*border\b", parsed),
          f"ambiguous self-referential border.color binding in {path.name}")

# All declared Qt metaobject headers must be compiled by the xmake moc rule.
xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
for path in (ROOT / "src/app").glob("*.hpp"):
    source = path.read_text(encoding="utf-8")
    if "Q_OBJECT" in source:
        check(f'"src/app/{path.name}"' in xmake, f"moc header not in xmake.lua: {path.name}")

# Qt defines `slots`, `signals`, and `emit` as macros unless built with
# QT_NO_KEYWORDS. They must not be used as regular variable names or captures.
controller_source = (ROOT / "src/app/archive_controller.cpp").read_text(encoding="utf-8")
check(not re.search(r"\b(?:const\s+)?auto\s+(?:slots|signals|emit)\s*=", controller_source),
      "Qt keyword macro used as a C++ variable name in archive_controller.cpp")
check(not re.search(r"\[[^]\n]*\b(?:slots|signals|emit)\b[^]\n]*\]\s*\(", controller_source),
      "Qt keyword macro used as a lambda capture in archive_controller.cpp")

# The short product description must be reflected in the running UI,
# Windows file properties, and the README—not just in documentation.
product_description = "Arky is a crossplatform archive manager supporting a range of formats."
windows_resource = ROOT / "src/app/arky.rc"
check(windows_resource.exists(), "Windows version resource missing")
if windows_resource.exists():
    check(f'VALUE "FileDescription", "{product_description}"' in windows_resource.read_text(encoding="utf-8"),
          "Windows executable FileDescription mismatch")
check(product_description in qml, "About Arky description missing from QML")
check(product_description in (ROOT / "README.md").read_text(encoding="utf-8"),
      "README project description mismatch")
check('add_files("src/app/arky.rc")' in xmake, "Windows version resource is not compiled by Xmake")
check('id: aboutDialog' in qml and 'onTriggered: aboutDialog.open()' in qml,
      "Help > About dialog is not wired to description")
check('QGuiApplication::setApplicationVersion("0.9.0")' in (ROOT / "src/app/main.cpp").read_text(encoding="utf-8"),
      "Qt application version not set")

if issues:
    for error in issues:
        print("FAIL:", error, file=sys.stderr)
    raise SystemExit(1)
print(f"PASS: {len(resources)} resource paths, QML brace structure, backend invokables/properties, moc headers")
