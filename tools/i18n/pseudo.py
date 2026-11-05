"""Build the pseudo-locale (po/qqq.po) from a gettext template.

    pseudo.py po/zacos9.pot /tmp/qqq.po

Every message grows by about a third and loses its plain vowels, so a
screen run with LANGUAGE=qqq shows what a longer translation will break.
It is for checking layouts, not for shipping: do not add qqq to LINGUAS.
"""
import json
import re
import sys

# Vowels only, all of which the bitmap fonts can draw: a fallback face would
# change the metrics and hide the layout problem this is looking for.
ACCENTS = str.maketrans("aeiouAEIOU", "áéíóúÁÉÍÓÚ")

FIELD = re.compile(r'^(msgctxt|msgid_plural|msgid|msgstr\[\d+\]|msgstr)\s+("(?:[^"\\]|\\.)*")\s*$')


def pseudo_text(s):
    """The message as the pseudo-locale writes it."""
    core = s.strip()
    if not core:
        return s
    lead = s[:len(s) - len(s.lstrip())]
    trail = s[len(s.rstrip()):]
    core = core.translate(ACCENTS)
    pad = max(4, len(core) // 3 + 1)
    return lead + "[" + core + "~" * pad + "]" + trail


def unquote(s):
    return json.loads(s)


def quote(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    s = s.replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")
    return '"' + s + '"'


def parse_block(lines):
    comments, fields, name = [], [], None
    for line in lines:
        if line.startswith("#"):
            comments.append(line)
            name = None
        elif FIELD.match(line):
            name, value = FIELD.match(line).groups()
            fields.append([name, unquote(value)])
        elif line.startswith('"') and name:
            fields[-1][1] += unquote(line.strip())
        else:
            raise SystemExit(f"pseudo.py: cannot read {line!r}")
    return comments, fields


def parse_po(text):
    blocks, cur = [], []
    for line in text.splitlines():
        if line.strip():
            cur.append(line)
        elif cur:
            blocks.append(parse_block(cur))
            cur = []
    if cur:
        blocks.append(parse_block(cur))
    return blocks


def translate_block(comments, fields):
    if not any(name == "msgid" for name, _ in fields):
        return comments, fields
    msgid = next(value for name, value in fields if name == "msgid")
    if msgid == "":
        for field in fields:
            if field[0] == "msgstr":
                field[1] = field[1].replace("charset=CHARSET", "charset=UTF-8")
                field[1] = re.sub(r"^Language:.*$", "Language: qqq", field[1], flags=re.M)
                if "Language:" not in field[1]:
                    field[1] += "Language: qqq\n"
                if "Plural-Forms:" not in field[1]:
                    field[1] += "Plural-Forms: nplurals=2; plural=(n != 1);\n"
        return comments, fields
    plural = next((value for name, value in fields if name == "msgid_plural"), None)
    head = [[name, value] for name, value in fields
            if name in ("msgctxt", "msgid", "msgid_plural")]
    if plural is None:
        return comments, head + [["msgstr", pseudo_text(msgid)]]
    return comments, head + [["msgstr[0]", pseudo_text(msgid)],
                             ["msgstr[1]", pseudo_text(plural)]]


def pseudo_po(text):
    """The template's text as a pseudo-locale catalog."""
    blocks = [translate_block(comments, fields) for comments, fields in parse_po(text)]
    out = []
    for comments, fields in blocks:
        out.extend(comments)
        out.extend(f"{name} {quote(value)}" for name, value in fields)
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: pseudo.py POT QQQ_PO")
    with open(argv[1], encoding="utf-8") as f:
        text = f.read()
    with open(argv[2], "w", encoding="utf-8") as f:
        f.write(pseudo_po(text))


if __name__ == "__main__":
    main(sys.argv)
