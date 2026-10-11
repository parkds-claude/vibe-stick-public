#!/usr/bin/env python3
"""USB 마이크 보관함의 크기를 정하려고 만든 모의 실험. 기기가 필요 없다.

    python3 cores3/tools/sim_flow.py

TinyUSB 0.19 의 흐름 조절 규칙(audio_device.c 의 audiod_tx_packet_size)을 그대로 옮겼다:
보관함이 절반보다 비면 1ms 에 15표본, 차면 17표본, 그 밖에는 16표본을 보내고, 한 번 조절하면
10묶음 동안은 조절하지 않는다. 여기에 "마이크가 8ms 마다 128표본을 내주고, 그것이 덩어리를 채울
때마다 넘어온다"는 기기의 모양과, 기기·맥의 시계 차이를 넣어 10분씩 돌린다.

볼 것: 빈 묶음(보관함이 바닥나 소리가 끊김)과 버린 바이트(가득 차 넘침)가 0 인가, 최소~최대가
바닥(0)과 천장(보관함 크기)에서 얼마나 떨어져 있는가.
결론: 보관함 48묶음 + 덩어리 128표본 → tusb_config.h, mic.cpp.
"""
import random
MIN, NOM, MAX = 30, 32, 34
SLOT = MAX - NOM
EP = 34

def run(ppm, depth_pkts, chunk, dma=128, seconds=600, jitter_ms=0.0, seed=1):
    DEPTH = depth_pkts * EP; PREFILL = (DEPTH // 2) & ~1
    fifo = 0; ring = 0; blackout = 0
    zlp = dropped = 0; lo = 10**9; hi = 0
    rnd = random.Random(seed)
    dma_period = dma / 16.0 * (1 + ppm / 1e6)   # ms (맥의 시계로)
    k = 0; phase = dma_period * rnd.random()
    carry = 0   # 덩어리에 아직 못 채운 표본
    prefilled = False
    for ms in range(seconds * 1000):
        if fifo < MIN: pkt = 0
        elif fifo < DEPTH // 2 - SLOT and not blackout: pkt = MIN; blackout = 10
        elif fifo > DEPTH // 2 + SLOT and not blackout: pkt = MAX; blackout = 10
        else:
            pkt = NOM
            if blackout: blackout -= 1
        pkt = min(pkt, EP, fifo); fifo -= pkt
        if ms > 100:
            zlp += pkt == 0; lo = min(lo, fifo); hi = max(hi, fifo)
        if not prefilled:
            fifo += PREFILL; prefilled = True; ring = 0
        # 이 1ms 사이에 끝난 DMA 읽기들(도착 시각에 ±jitter 의 흔들림 — 쌓이지 않는 흔들림)
        while True:
            t_arr = phase + k * dma_period + (rnd.uniform(0, jitter_ms) if jitter_ms else 0)
            if t_arr > ms: break
            k += 1; carry += dma
            while carry >= chunk:
                carry -= chunk; ring += chunk * 2
        room = DEPTH - fifo
        moved = min(ring, room); dropped += ring - moved; fifo += moved; ring = 0
    return zlp, dropped, lo, hi, DEPTH

def table(depth_pkts, chunk, jitter=0.0):
    worst = [0, 0, 10**9, 0]
    for ppm in (-1000, -300, -100, 0, 100, 300, 1000):
        for seed in (1, 2, 3):
            z, d, lo, hi, DEPTH = run(ppm, depth_pkts, chunk, jitter_ms=jitter, seed=seed)
            worst = [worst[0] + z, worst[1] + d, min(worst[2], lo), max(worst[3], hi)]
    print(f"보관함 {depth_pkts:3d}묶음({DEPTH}B, 절반 {DEPTH//2//32}ms) 덩어리 {chunk:3d}표본 흔들림 {jitter:.0f}ms → "
          f"빈 묶음 {worst[0]:6d}, 버린 바이트 {worst[1]:6d}, 최소~최대 {worst[2]}~{worst[3]}")

print("(시계 차이 -1000~+1000ppm 일곱 가지 x 시작 엇갈림 세 가지, 각 10분)")
for depth in (36, 48, 64):
    for chunk in (160, 128, 64):
        table(depth, chunk)
print("도착이 0~4ms 늦어지는 흔들림까지:")
for depth in (48, 64):
    for chunk in (160, 128):
        table(depth, chunk, jitter=4.0)
