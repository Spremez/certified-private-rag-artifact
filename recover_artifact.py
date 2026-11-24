"""Reassemble the frozen experiment ZIP from a downloaded repository snapshot."""
import argparse
import base64
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import tempfile
import zipfile


ARCHIVE = "certified-rag-public.zip"
SHA256 = "ff6365a125a975e2b9b3256e3861bd65fc38ca011fe7144959595a4f04b854e6"
PARTS = "artifact-parts.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, help="Repository ZIP downloaded from the review link or GitHub")
    parser.add_argument("--output", type=Path, default=Path(ARCHIVE))
    args = parser.parse_args()
    folder = Path(__file__).resolve().parent
    with ExitStack() as stack:
        if args.snapshot:
            snapshot = stack.enter_context(zipfile.ZipFile(args.snapshot))
            candidates = [name for name in snapshot.namelist() if Path(name).name == PARTS]
            if len(candidates) != 1:
                raise ValueError("Expected exactly one artifact-parts.json in the repository ZIP.")
            prefix = candidates[0].removesuffix(PARTS)

        def read(name):
            if args.snapshot:
                return snapshot.read(prefix + name)
            return (folder / name).read_bytes()

        manifest = json.loads(read(PARTS))
        if manifest["sha256"] != SHA256:
            raise ValueError("This manifest does not describe the frozen experiment ZIP.")
        scratch = stack.enter_context(tempfile.TemporaryDirectory(dir=args.output.parent))
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
