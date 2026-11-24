"""Reassemble the frozen experiment ZIP from local or anonymously hosted parts."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import tempfile
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, build_opener


ARCHIVE = "certified-rag-public.zip"
SHA256 = "ff6365a125a975e2b9b3256e3861bd65fc38ca011fe7144959595a4f04b854e6"
PARTS = "artifact-parts.json"


class SameHostRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urlsplit(newurl)[:2] != ("https", "anonymous.4open.science"):
            raise ValueError("The mirror tried to redirect the download to another host.")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mirror", help="Anonymous GitHub repository URL from the paper")
    parser.add_argument("--output", type=Path, default=Path(ARCHIVE))
    args = parser.parse_args()
    folder = Path(__file__).resolve().parent
    if args.mirror:
        match = re.fullmatch(r"https://anonymous\.4open\.science/r/([A-Za-z0-9_-]+)(?:/README\.md|/)?", args.mirror)
        if not match:
            parser.error("Use the anonymous repository or its README.md URL from the paper.")
        root = f"https://anonymous.4open.science/api/repo/{match[1]}/file/"
        guest = build_opener(SameHostRedirects())

    def read(name):
        if args.mirror:
            with guest.open(root + name, timeout=120) as response:
                return response.read()
        return (folder / name).read_bytes()

    manifest = json.loads(read(PARTS))
    if manifest["sha256"] != SHA256:
        raise ValueError("This manifest does not describe the frozen experiment ZIP.")
    with tempfile.TemporaryDirectory(dir=args.output.parent) as scratch:
        assembled = Path(scratch) / ARCHIVE
        checksum = hashlib.sha256()
        with assembled.open("wb") as output:
            for number, record in enumerate(manifest["parts"], 1):
                name = f"artifact-{number:03d}.b64"
                if record["name"] != name:
                    raise ValueError(f"Unexpected part at position {number}")
                data = base64.b64decode(b"".join(read(name).split()), validate=True)
                if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                    raise ValueError(f"Wrong bytes in {name}")
                checksum.update(data)
                output.write(data)
        if checksum.hexdigest() != SHA256 or assembled.stat().st_size != manifest["bytes"]:
            raise ValueError("The assembled archive failed verification.")
        if args.output.exists():
            with args.output.open("rb") as existing:
                if hashlib.file_digest(existing, "sha256").hexdigest() != SHA256:
                    raise FileExistsError(args.output)
        else:
            args.output.hardlink_to(assembled)
    print(f"{SHA256}  {args.output}")


if __name__ == "__main__":
    main()
