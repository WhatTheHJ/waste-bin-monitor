#!/usr/bin/env python3
"""
route_view.py

공중에 고정한 웹캠으로 지도를 위에서 비추고, 포화도에 따라 수거 경로를 그린다.
영상 분석은 하지 않는다. 지도 네 모서리를 한 번 찍어 두면(--calibrate)
도트매트릭스와 같은 8x8 칸 좌표(1부터, 왼쪽 위가 1,1)로 쓰레기통과 길 위치를 계산해 선만 긋는다.

  python3 route_view.py --calibrate   지도 네 모서리 찍기 (처음 한 번, 카메라를 옮기면 다시)
  python3 route_view.py               실행: 창 표시 + route.jpg / status.json 저장 (PHP가 읽음)
  python3 route_view.py --demo        DB 없이 가짜 포화도로 실행 (창에서 1/2/3 키로 A/B/C +10%)
  python3 route_view.py --camera -1   웹캠 없이 그림판 위에 그리기
"""

import argparse
import itertools
import json
import math
import os
import time
from collections import deque

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GRID = 8
WIN = "route_view"
CAL_WIN = "calibrate"
FONT = cv2.FONT_HERSHEY_SIMPLEX
AA = cv2.LINE_AA
ROUTE_COLOR = (255, 160, 0)          # BGR, 하늘색 계열

try:    # 카메라 재연결 때마다 OpenCV 내부 경고가 쏟아지지 않게 (우리 메시지는 따로 출력)
    cv2.utils.logging.setLogLevel(cv2.utils.logging.LOG_LEVEL_SILENT)
except AttributeError:
    pass


# ---------------------------------------------------------------- 설정

def load_config(path):
    src = path if os.path.exists(path) else os.path.join(HERE, "config.example.json")
    if src != path:
        print(f"[info] {path} 없음 -> config.example.json 사용", flush=True)
    with open(src, encoding="utf-8") as f:
        return json.load(f)


def corners_for(cfg, w, h):
    """보정 때 해상도와 지금 해상도가 다르면 비율대로 맞춘다."""
    c = cfg.get("corners")
    if not c:
        return None
    cw, ch = cfg.get("calib_size") or [w, h]
    return [[x * w / cw, y * h / ch] for x, y in c]


# ---------------------------------------------------------------- 칸 좌표 -> 화면 좌표

class GridMap:
    def __init__(self, corners, w, h):
        self.size = (w, h)
        if not corners:                       # 보정 전: 화면 가운데 정사각형
            s = min(w, h) * 0.8
            x0, y0 = (w - s) / 2, (h - s) / 2
            corners = [[x0, y0], [x0 + s, y0], [x0 + s, y0 + s], [x0, y0 + s]]
        src = np.float32([[0, 0], [GRID, 0], [GRID, GRID], [0, GRID]])
        self.H = cv2.getPerspectiveTransform(src, np.float32(corners))
        d1 = math.dist(self.edge(0, 0), self.edge(GRID, 0)) / GRID
        d2 = math.dist(self.edge(0, 0), self.edge(0, GRID)) / GRID
        self.cell = max(8, int((d1 + d2) / 2))

    def edge(self, gx, gy):
        """칸 경계 좌표(0~8)를 화면 픽셀로."""
        u, v, s = self.H @ np.array([gx, gy, 1.0])
        return int(round(u / s)), int(round(v / s))

    def px(self, x, y):
        """1부터 센 칸 (x, y)의 가운데를 화면 픽셀로."""
        return self.edge(x - 0.5, y - 0.5)

    def cell_poly(self, x, y):
        return np.array([self.edge(x - 1, y - 1), self.edge(x, y - 1),
                         self.edge(x, y), self.edge(x - 1, y)], np.int32)


# ---------------------------------------------------------------- 경로 (길 칸을 따라 BFS)

def road_cells(road):
    return {(x + 1, y + 1) for y, row in enumerate(road) for x, c in enumerate(row) if c == "#"}


def nearest_road(cells, p):
    return min(cells, key=lambda c: (abs(c[0] - p[0]) + abs(c[1] - p[1]), c[1], c[0]))


def road_path(cells, a, b):
    prev = {a: None}
    q = deque([a])
    while q:
        c = q.popleft()
        if c == b:
            break
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + dx, c[1] + dy)
            if n in cells and n not in prev:
                prev[n] = c
                q.append(n)
    if b not in prev:
        return None
    path, c = [], b
    while c is not None:
        path.append(c)
        c = prev[c]
    return path[::-1]


def leg(cells, p, q):
    """p -> 가까운 길 -> 길 따라 -> q. 길이 없거나 끊겼으면 직선."""
    if not cells:
        return [p, q]
    mid = road_path(cells, nearest_road(cells, p), nearest_road(cells, q)) or []
    return [p] + mid + [q]


def path_len(pts):
    return sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))


def plan_route(cells, bins, targets, depot):
    """대상 통의 모든 방문 순서(최대 6가지) 중 가장 짧은 것과 그 좌표 목록."""
    if not targets:
        return [], []
    if depot is None and len(targets) == 1:
        return list(targets), []
    best = None
    for order in itertools.permutations(targets):
        stops = [bins[n] for n in order]
        if depot:
            stops = [depot] + stops + [depot]
        pts = [stops[0]]
        for a, b in zip(stops, stops[1:]):
            for p in leg(cells, a, b)[1:]:
                if p != pts[-1]:
                    pts.append(p)
        d = path_len(pts)
        if best is None or d < best[0]:
            best = (d, list(order), pts)
    return best[1], best[2]


# ---------------------------------------------------------------- 그리기

def level_color(p):
    """아두이노 LED와 같은 기준."""
    if p < 0:
        return (150, 150, 150)
    if p <= 40:
        return (80, 200, 80)
    if p <= 75:
        return (0, 210, 255)
    return (60, 60, 230)


def put_text(img, text, org, color=(255, 255, 255), scale=0.5):
    cv2.putText(img, text, org, FONT, scale, (0, 0, 0), 3, AA)
    cv2.putText(img, text, org, FONT, scale, color, 1, AA)


def blank_board(w, h, gm, cells):
    img = np.full((h, w, 3), 60, np.uint8)
    for x, y in cells:
        cv2.fillPoly(img, [gm.cell_poly(x, y)], (110, 110, 110))
    return img


def draw_grid(img, gm, cells):
    for i in range(GRID + 1):
        cv2.line(img, gm.edge(i, 0), gm.edge(i, GRID), (200, 200, 200), 1, AA)
        cv2.line(img, gm.edge(0, i), gm.edge(GRID, i), (200, 200, 200), 1, AA)
    for x, y in cells:
        cv2.polylines(img, [gm.cell_poly(x, y)], True, (0, 200, 255), 1, AA)


def draw_overlay(img, gm, bins, cells, depot, values, order, route, warns, grid):
    h, w = img.shape[:2]
    cell = gm.cell
    if grid:
        draw_grid(img, gm, cells)

    if len(route) >= 2:
        pts = np.array([gm.px(*p) for p in route], np.int32).reshape(-1, 1, 2)
        t = max(3, cell // 6)
        cv2.polylines(img, [pts], False, (0, 0, 0), t + 4, AA)
        cv2.polylines(img, [pts], False, ROUTE_COLOR, t, AA)

    if depot:
        c = gm.px(*depot)
        r = max(5, int(cell * 0.3))
        cv2.rectangle(img, (c[0] - r, c[1] - r), (c[0] + r, c[1] + r), (255, 255, 255), 2)

    r = max(6, int(cell * 0.35))
    blink = int(time.time() * 3) % 2
    for name, pos in bins.items():
        p = values.get(name, -1)
        c = gm.px(*pos)
        color = (0, 0, 120) if (p > 90 and blink) else level_color(p)
        cv2.circle(img, c, r, color, -1, AA)
        cv2.circle(img, c, r, (0, 0, 0), 2, AA)
        put_text(img, name, (c[0] - 5, c[1] + 5))
        label = f"{p}%" if p >= 0 else "--"
        if name in order:
            cv2.circle(img, c, r + 4, (255, 255, 255), 2, AA)
            label = f"#{order.index(name) + 1} {label}"
        put_text(img, label, (c[0] + r + 6, c[1] + 5))

    cv2.rectangle(img, (0, 0), (w, 24), (0, 0, 0), -1)
    if order:
        stops = (["DEPOT"] + order + ["DEPOT"]) if depot else order
        put_text(img, "Route: " + " > ".join(stops), (8, 17))
    else:
        put_text(img, "No target", (8, 17))
    put_text(img, time.strftime("%H:%M:%S"), (w - 78, 17))
    if warns:
        put_text(img, "  ".join(warns), (8, 44), (0, 0, 255), 0.6)


# ---------------------------------------------------------------- 입력: 카메라 / DB

class Camera:
    def __init__(self, index, w, h):
        self.index, self.w, self.h = index, w, h
        self.cap = None
        self.retry_at = 0.0
        self.warned = False

    def read(self):
        if self.index < 0:
            return None
        now = time.time()
        if self.cap is None:
            if now < self.retry_at:
                return None
            # 리눅스는 V4L2로 바로 연다 (GStreamer 경고 줄이 쏟아지지 않게)
            backend = cv2.CAP_V4L2 if os.name == "posix" else cv2.CAP_ANY
            cap = cv2.VideoCapture(self.index, backend)
            if not cap.isOpened():
                cap.release()
                self.retry_at = now + 3
                if not self.warned:
                    print(f"[camera] {self.index}번 카메라를 열 수 없음. 3초마다 다시 시도", flush=True)
                    self.warned = True
                return None
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.w)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.h)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)   # 오래된 프레임이 쌓이지 않게
            self.cap, self.warned = cap, False
        ok, frame = self.cap.read()
        if not ok:
            self.cap.release()
            self.cap = None
            self.retry_at = now + 3
            return None
        return frame


class BinSource:
    """food_bin 최신 한 줄을 읽는다. --demo면 DB 없이 가짜 값."""

    def __init__(self, db, demo):
        self.db, self.demo = db, demo
        self.values = {"A": 30, "B": 80, "C": 95} if demo else {"A": -1, "B": -1, "C": -1}
        self.ok, self.error, self.data_time = True, "", ""
        self.conn = None

    def bump(self, name):
        v = self.values.get(name, 0) + 10
        self.values[name] = 0 if v > 100 else v

    def poll(self):
        if self.demo:
            self.data_time = time.strftime("%Y-%m-%d %H:%M:%S")
            return
        try:
            import pymysql
            if self.conn is None:
                self.conn = pymysql.connect(
                    host=self.db.get("host", "127.0.0.1"), port=int(self.db.get("port", 3306)),
                    user=self.db.get("user", ""), password=self.db.get("password", ""),
                    database=self.db.get("database", ""), charset="utf8mb4",
                    connect_timeout=3, autocommit=True)   # autocommit: 매번 새 행이 보이게
            with self.conn.cursor() as cur:
                cur.execute("SELECT bin_a, bin_b, bin_c, created_at FROM food_bin ORDER BY id DESC LIMIT 1")
                row = cur.fetchone()
            if row:
                self.values = {"A": int(row[0]), "B": int(row[1]), "C": int(row[2])}
                self.data_time = str(row[3])
            self.ok, self.error = True, ""
        except Exception as e:
            if self.ok:
                print(f"[db] {e}", flush=True)
            self.ok, self.error = False, str(e)
            if self.conn is not None:
                try:
                    self.conn.close()
                except Exception:
                    pass
                self.conn = None


# ---------------------------------------------------------------- 출력 (PHP용)

def save_outputs(out_dir, img, status, quiet=False):
    try:
        os.makedirs(out_dir, exist_ok=True)
        tmp = os.path.join(out_dir, "route.tmp.jpg")      # 반쯤 쓴 파일을 웹이 읽지 않게
        cv2.imwrite(tmp, img, [cv2.IMWRITE_JPEG_QUALITY, 80])
        os.replace(tmp, os.path.join(out_dir, "route.jpg"))
        tmp = os.path.join(out_dir, "status.tmp.json")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(status, f, ensure_ascii=False)
        os.replace(tmp, os.path.join(out_dir, "status.json"))
        return True
    except OSError as e:
        if not quiet:
            print(f"[save] {out_dir}: {e}  (폴더 권한 확인. 고쳐지면 자동으로 다시 저장)", flush=True)
        return False


# ---------------------------------------------------------------- 실행

def run(cfg, cam, src, out_dir, show):
    bins = {k: tuple(v) for k, v in cfg["bins"].items()}
    cells = road_cells(cfg.get("road", []))
    depot = tuple(cfg["depot"]) if cfg.get("depot") else None
    threshold = cfg.get("threshold", 75)
    fps = max(1, cfg.get("fps", 10))
    poll_s, save_s = cfg.get("poll_seconds", 2), cfg.get("save_seconds", 2)

    if not cfg.get("corners"):
        print("[info] 보정 전입니다. --calibrate 로 지도 네 모서리를 먼저 찍으세요.", flush=True)
    if out_dir:
        print(f"[info] 저장 폴더: {out_dir}", flush=True)

    gm, last_frame = None, None
    next_poll = next_save = 0.0
    order, route = [], []
    save_ok = True
    if show:
        cv2.namedWindow(WIN, cv2.WINDOW_NORMAL)

    while True:
        t0 = time.time()
        frame = cam.read()
        if frame is not None:
            last_frame = frame
        if last_frame is not None:
            h, w = last_frame.shape[:2]
        else:
            h, w = cfg.get("height", 480), cfg.get("width", 640)
        if gm is None or gm.size != (w, h):
            gm = GridMap(corners_for(cfg, w, h), w, h)

        if frame is not None:
            img = frame
        elif last_frame is not None:
            img = last_frame.copy()
        else:
            img = blank_board(w, h, gm, cells)

        if t0 >= next_poll:
            src.poll()
            next_poll = t0 + poll_s
            targets = [n for n in bins if src.values.get(n, -1) > threshold]   # 도트매트릭스와 같은 "초과"
            order, route = plan_route(cells, bins, targets, depot)

        warns = []
        if cam.index >= 0 and frame is None:
            warns.append("CAMERA LOST")
        if not src.ok:
            warns.append("DB ERROR")
        draw_overlay(img, gm, bins, cells, depot, src.values, order, route, warns,
                     cfg.get("draw_grid", False) or last_frame is None)

        if out_dir and t0 >= next_save:
            next_save = t0 + save_s
            save_ok = save_outputs(out_dir, img, {
                "time": time.strftime("%Y-%m-%d %H:%M:%S"), "unix": int(t0),
                "data_time": src.data_time, "bins": src.values, "threshold": threshold,
                "order": order, "db_ok": src.ok, "camera_ok": cam.index < 0 or frame is not None,
            }, quiet=not save_ok)
        if show:
            cv2.imshow(WIN, img)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
            if src.demo and key in (ord("1"), ord("2"), ord("3")):
                src.bump("ABC"[key - ord("1")])
                next_poll = 0
            if cv2.getWindowProperty(WIN, cv2.WND_PROP_VISIBLE) < 1:
                break
        time.sleep(max(0.0, 1.0 / fps - (time.time() - t0)))

    if show:
        cv2.destroyAllWindows()


def calibrate(cfg, cam, path):
    """지도(8x8 칸 영역)의 네 모서리를 왼쪽 위부터 시계 방향으로 클릭해 저장."""
    names = ["top-left", "top-right", "bottom-right", "bottom-left"]
    bins = {k: tuple(v) for k, v in cfg["bins"].items()}
    cells = road_cells(cfg.get("road", []))
    pts, last = [], None

    def on_mouse(ev, x, y, flags, param):
        if ev == cv2.EVENT_LBUTTONDOWN and len(pts) < 4:
            pts.append([x, y])

    cv2.namedWindow(CAL_WIN, cv2.WINDOW_AUTOSIZE)   # 클릭 좌표 = 영상 픽셀
    cv2.setMouseCallback(CAL_WIN, on_mouse)

    while True:
        f = cam.read()
        if f is not None:
            last = f
        h, w = last.shape[:2] if last is not None else (cfg.get("height", 480), cfg.get("width", 640))
        img = last.copy() if last is not None else np.full((h, w, 3), 60, np.uint8)

        for i, p in enumerate(pts):
            cv2.circle(img, tuple(p), 5, (0, 0, 255), -1, AA)
            put_text(img, str(i + 1), (p[0] + 7, p[1] - 7), (0, 0, 255))
        if len(pts) == 4:
            gm = GridMap(pts, w, h)
            draw_grid(img, gm, cells)
            for name, pos in bins.items():
                c = gm.px(*pos)
                cv2.circle(img, c, max(5, gm.cell // 3), (60, 60, 230), 2, AA)
                put_text(img, name, (c[0] - 5, c[1] + 5))
            put_text(img, "check grid, then s: save", (8, 20), (0, 255, 255))
        else:
            put_text(img, f"click {len(pts) + 1}/4: {names[len(pts)]} corner of map", (8, 20), (0, 255, 255))
        put_text(img, "z: undo  r: reset  s: save  q: quit", (8, h - 10))

        cv2.imshow(CAL_WIN, img)
        key = cv2.waitKey(30) & 0xFF
        if key in (ord("q"), 27):
            print("[calibrate] 저장하지 않고 종료", flush=True)
            break
        if key == ord("z") and pts:
            pts.pop()
        elif key == ord("r"):
            pts.clear()
        elif key == ord("s") and len(pts) == 4:
            cfg["corners"], cfg["calib_size"] = pts, [w, h]
            with open(path, "w", encoding="utf-8") as fp:
                json.dump(cfg, fp, ensure_ascii=False, indent=2)
            cv2.imwrite(os.path.join(HERE, "calibration.jpg"), img)
            print(f"[calibrate] 저장: {path} (확인용 calibration.jpg)", flush=True)
            break
    cv2.destroyAllWindows()


def main():
    ap = argparse.ArgumentParser(description="웹캠 탑뷰 위에 수거 경로 그리기")
    ap.add_argument("--config", default=os.path.join(HERE, "config.json"))
    ap.add_argument("--calibrate", action="store_true", help="지도 네 모서리 찍기")
    ap.add_argument("--demo", action="store_true", help="DB 없이 가짜 포화도 (창에서 1/2/3 키로 +10%%)")
    ap.add_argument("--camera", type=int, help="카메라 번호 (-1이면 카메라 없이)")
    ap.add_argument("--no-window", action="store_true", help="창 없이 파일만 저장")
    ap.add_argument("--out", help="route.jpg / status.json 저장 폴더 (빈 문자열이면 저장 안 함)")
    args = ap.parse_args()

    cfg = load_config(args.config)
    if args.camera is not None:
        cfg["camera"] = args.camera
    cam = Camera(cfg.get("camera", 0), cfg.get("width", 640), cfg.get("height", 480))

    if args.calibrate:
        calibrate(cfg, cam, args.config)
        return

    out_dir = args.out if args.out is not None else ("out" if args.demo else cfg.get("output_dir", ""))
    if out_dir and not os.path.isabs(out_dir):
        out_dir = os.path.join(HERE, out_dir)
    run(cfg, cam, BinSource(cfg.get("db", {}), args.demo), out_dir, not args.no_window)


if __name__ == "__main__":
    main()
