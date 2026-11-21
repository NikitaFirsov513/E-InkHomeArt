"""Generate private build-only C strings; never print credential contents."""
import argparse, json
from pathlib import Path

def generate(root, output):
    def read(name):
        p = root / "secrets" / name
        return json.loads(p.read_text(encoding="utf-8-sig")) if p.exists() else {}
    wifi, oauth = read("wifi.json"), read("yandex.json")
    values = {"HOME_WIFI_SSID": wifi.get("ssid", ""),
              "HOME_WIFI_PASSWORD": wifi.get("password", ""),
              "HOME_OAUTH_ID": oauth.get("client_id", ""),
              "HOME_OAUTH_SECRET": oauth.get("client_secret", "")}
    for key, value in values.items():
        if not isinstance(value, str) or "\0" in value:
            raise ValueError(f"Invalid string: {key}")
    ssid, password = values["HOME_WIFI_SSID"], values["HOME_WIFI_PASSWORD"]
    if len(ssid.encode()) > 32 or (password and not 8 <= len(password.encode()) <= 63):
        raise ValueError("Wi-Fi SSID/password length is invalid")
    for key in ("HOME_OAUTH_ID", "HOME_OAUTH_SECRET"):
        value = values[key]
        if len(value) > (128 if key == "HOME_OAUTH_ID" else 256) or any(not 33 <= ord(c) <= 126 for c in value):
            raise ValueError(f"Invalid OAuth credential: {key}")
    # Byte escapes avoid C encoding, quoting and universal-character edge cases.
    quote = lambda v: '\"' + ''.join('\\%03o' % b for b in v.encode("utf-8")) + '\"'
    text = "#pragma once\n" + "".join(f"#define {k} {quote(v)}\n" for k,v in values.items())
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text, encoding="ascii")

if __name__ == "__main__":
    parser=argparse.ArgumentParser(); parser.add_argument("root", type=Path); parser.add_argument("output", type=Path)
    args=parser.parse_args(); generate(args.root, args.output)
