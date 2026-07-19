#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WISH 보드 일괄 빌드 스크립트 (baram build_venom.py 방식).

각 보드를 build-<board>/ 디렉토리에 서로 간섭 없이 독립 빌드하고,
결과물(uf2 + 대응 VIA JSON)을 날짜+리비전 기반 통합 폴더로 모은다.

  output/WISH-V<YYMMDD>R<n>/
    45k/
      WISH45-8K-V260718R1.uf2
      WISH45-8K-VIA.json

보드는 keyboards/baram/<board>/config.h 존재 여부로 자동 탐색한다
(새 보드 폴더만 추가하면 자동으로 빌드 대상에 포함).

사용법:
    python3 tools/build_wish.py            # 전체 보드 빌드 (리비전 자동 증가)
    python3 tools/build_wish.py 45k        # 특정 보드만
    python3 tools/build_wish.py -c         # build-<board>/ 정리 후 빌드
    python3 tools/build_wish.py -j 8       # 병렬 job 수
    python3 tools/build_wish.py --rev 3    # 리비전 직접 지정 (R3)
    python3 tools/build_wish.py --list     # 보드 목록만 출력
"""

import argparse
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys

# tools/ 의 상위 = 프로젝트 루트
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# QMK 키보드 경로 기준 (CMake 의 -DKEYBOARD_PATH 와 동일 형식)
KEYBOARD_BASE = "/keyboards/baram"

# 보드 소스 폴더 (VIA JSON 등을 찾는 기준)
KEYBOARD_SRC_DIR = os.path.join(
    ROOT_DIR, "src", "ap", "modules", "qmk", "keyboards", "baram")

OUTPUT_BASE_DIR = os.path.join(ROOT_DIR, "output")


def discover_boards():
    """keyboards/baram/<board>/config.h 존재하는 보드를 자동 탐색."""
    boards = []
    if os.path.isdir(KEYBOARD_SRC_DIR):
        for name in sorted(os.listdir(KEYBOARD_SRC_DIR)):
            if os.path.isfile(os.path.join(KEYBOARD_SRC_DIR, name, "config.h")):
                boards.append(name)
    return boards


def resolve_output_dir(rev):
    """날짜 + 리비전 기반 통합 폴더 경로. rev=None 이면 자동 증가."""
    date_tag = datetime.date.today().strftime("V%y%m%d")   # 예: V260719
    prefix = "WISH-" + date_tag + "R"

    if rev is None:
        max_rev = 0
        if os.path.isdir(OUTPUT_BASE_DIR):
            for name in os.listdir(OUTPUT_BASE_DIR):
                m = re.match("^" + re.escape(prefix) + r"(\d+)$", name)
                if m:
                    max_rev = max(max_rev, int(m.group(1)))
        rev = max_rev + 1

    return os.path.join(OUTPUT_BASE_DIR, prefix + str(rev))


def find_via_json(board):
    """보드에 대응하는 VIA JSON 경로. json/*VIA*.json 을 글롭."""
    json_dir = os.path.join(KEYBOARD_SRC_DIR, board, "json")
    hits = (glob.glob(os.path.join(json_dir, "*VIA*.json")) +
            glob.glob(os.path.join(json_dir, "*VIA*.JSON")))
    return hits[0] if hits else None


def run(cmd):
    print("  $ " + " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT_DIR, check=True)


def build_one(board, jobs, clean):
    """단일 보드를 build-<board>/ 에 빌드하고 생성된 uf2 목록을 반환."""
    keyboard_path = KEYBOARD_BASE + "/" + board
    build_dir = os.path.join(ROOT_DIR, "build-" + board)

    print("=" * 70)
    print("[BUILD] {}  ({})".format(board, keyboard_path))
    print("=" * 70)

    if clean and os.path.isdir(build_dir):
        print("  clean: remove {}".format(build_dir))
        shutil.rmtree(build_dir)

    run(["cmake", "-S", ".", "-B", build_dir,
         "-DKEYBOARD_PATH=" + keyboard_path])
    run(["cmake", "--build", build_dir, "-j", str(jobs)])

    return sorted(glob.glob(os.path.join(build_dir, "*.uf2")))


def main():
    all_boards = discover_boards()

    parser = argparse.ArgumentParser(description="WISH 보드 일괄 빌드")
    parser.add_argument("boards", nargs="*",
                        help="빌드할 보드 (미지정 시 전체)")
    parser.add_argument("-c", "--clean", action="store_true",
                        help="build-<board>/ 삭제 후 빌드")
    parser.add_argument("-j", "--jobs", type=int, default=10,
                        help="병렬 빌드 job 수 (기본 10)")
    parser.add_argument("-r", "--rev", type=int, default=None,
                        help="통합 폴더 리비전 직접 지정 (미지정 시 자동 증가)")
    parser.add_argument("--list", action="store_true",
                        help="보드 목록만 출력")
    parser.add_argument("--no-zip", action="store_true",
                        help="릴리즈 zip 생성 안 함")
    args = parser.parse_args()

    if args.list:
        print("보드: " + (", ".join(all_boards) if all_boards else "(없음)"))
        return 0

    if not all_boards:
        print("keyboards/baram/ 에 빌드할 보드가 없습니다.")
        return 1

    targets = args.boards if args.boards else all_boards

    unknown = [b for b in targets if b not in all_boards]
    if unknown:
        print("알 수 없는 보드: {}".format(", ".join(unknown)))
        print("사용 가능: {}".format(", ".join(all_boards)))
        return 1

    output_dir = resolve_output_dir(args.rev)
    os.makedirs(output_dir, exist_ok=True)
    print("통합 폴더: {}".format(output_dir))

    results = {}
    collected = []
    for board in targets:
        try:
            uf2_files = build_one(board, args.jobs, args.clean)

            board_dir = os.path.join(output_dir, board)
            os.makedirs(board_dir, exist_ok=True)

            for src in uf2_files:
                dst = os.path.join(board_dir, os.path.basename(src))
                shutil.copy2(src, dst)
                collected.append(dst)

            via_json = find_via_json(board)
            if via_json:
                dst = os.path.join(board_dir, os.path.basename(via_json))
                shutil.copy2(via_json, dst)
                collected.append(dst)
            else:
                print("  [경고] VIA JSON 을 찾을 수 없음: {}".format(board))

            results[board] = "OK"
        except subprocess.CalledProcessError:
            results[board] = "FAIL"

    print()
    print("=" * 70)
    print("빌드 결과")
    print("=" * 70)
    for board in targets:
        print("  {:16s} : {}".format(board, results.get(board, "?")))
    print()
    print("생성 파일 ({} 개) -> {}".format(len(collected), output_dir))
    for path in collected:
        print("  - " + os.path.relpath(path, output_dir))

    all_ok = all(v == "OK" for v in results.values())

    # 릴리즈 zip : 전체 보드를 통째로 빌드해 모두 성공했을 때만 생성
    # (output/WISH-V<날짜>R<n>.zip -> 배포 이미지)
    if all_ok and not args.no_zip and set(targets) == set(all_boards):
        zip_path = shutil.make_archive(output_dir, "zip",
                                       root_dir=OUTPUT_BASE_DIR,
                                       base_dir=os.path.basename(output_dir))
        print()
        print("릴리즈 zip -> {}".format(os.path.relpath(zip_path, ROOT_DIR)))

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
