import subprocess
import time
from subprocess import PIPE


def run(cmd: str | list[str]):
    stime = time.time()
    cmd = " ".join(cmd) if isinstance(cmd, list) else cmd
    res = subprocess.run(f"sh -c '{cmd}'", shell=True, text=True, stdout=PIPE, stderr=PIPE)
    assert res.returncode == 0, f"Failed to run command: {cmd}"
    return res.stdout + res.stderr, time.time() - stime


def parse_final(out: str):
    out = [x for x in out.split("\n") if "it/s ~" in x][-1]
    return out.split(" ~ ")[-1].replace(",", "").strip()


def check_add(hosts: list[str]):
    out, elapsed = run(["make", "mul"])
    ref_out = parse_final(out)
    ref_addr33 = set([x.strip() for x in out.split("\n") if x.startswith("addr33:")])
    ref_addr65 = set([x.strip() for x in out.split("\n") if x.startswith("addr65:")])
    print(f">>> running cmd=add as ref ~ {elapsed:.2f}s")

    for host in hosts:
        print(f">>> running cmd=add {host}... ", end="")
        out, elapsed = run(["make", "remote", f"host={host}", "cmd=mul"])
        out = parse_final(out)
        addr33 = set([x.strip() for x in out.split("\n") if x.startswith("addr33:")])
        addr65 = set([x.strip() for x in out.split("\n") if x.startswith("addr65:")])
        if ref_out == out and ref_addr33 == addr33 and ref_addr65 == addr65:
            print(f"ok ~ {elapsed:.2f}s")
        else:
            print(f"failed ~ {elapsed:.2f}s")
            print(f"{ref_out=} != {out=}") if out != ref_out else None
            print(f"{ref_addr33=} != {addr33=}") if addr33 != ref_addr33 else None
            print(f"{ref_addr65=} != {addr65=}") if addr65 != ref_addr65 else None


def check_mul(hosts: list[str]):
    out, elapsed = run(["make", "mul"])
    ref_out = parse_final(out)
    print(f">>> running cmd=mul as ref ~ {elapsed:.2f}s")

    for host in hosts:
        print(f">>> running cmd=mul {host}... ", end="")
        out, elapsed = run(["make", "remote", f"host={host}", "cmd=mul"])
        out = parse_final(out)
        if ref_out == out:
            print(f"ok ~ {elapsed:.2f}s")
        else:
            print(f"failed ~ {elapsed:.2f}s")
            print(f"{ref_out=} != {out=}") if out != ref_out else None


def check_blf(hosts: list[str]):
    out, elapsed = run(["make", "blf"])
    ref_out = parse_final(out)
    print(f">>> running cmd=blf as ref ~ {elapsed:.2f}s")

    for host in hosts:
        print(f">>> running cmd=blf {host}... ", end="")
        out, elapsed = run(["make", "remote", f"host={host}", "cmd=blf"])
        out = parse_final(out)
        if ref_out == out:
            print(f"ok ~ {elapsed}s")
        else:
            print(f"failed ~ {elapsed}s")
            print(f"{ref_out=} != {out=}") if out != ref_out else None


def main():
    hosts = ["user@colima", "user@mele.local"]
    funcs = [check_add, check_mul, check_blf]
    for func in funcs:
        print(f"--- {func.__name__} ---")
        func(hosts)


if __name__ == "__main__":
    main()
