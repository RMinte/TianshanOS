#!/usr/bin/env python3
"""
BDF to FNT converter for TianShanOS LED Matrix

将 BDF 位图字体转换为 ts_led_font.c 兼容的 FNT 格式。
BDF 是标准位图字体格式，由 otf2bdf 等工具生成。

用法:
    python3 bdf2fnt.py input.bdf output.fnt [--width W] [--height H]

FNT 格式 (兼容 ts_led_font.c):
    Header (16 bytes):
        - magic: "TFNT" (4 bytes)
        - version: uint8_t = 1
        - width: uint8_t
        - height: uint8_t
        - flags: uint8_t = 0
        - glyph_count: uint32_t (little-endian)
        - index_offset: uint32_t (little-endian)
    
    Index (glyph_count * 6 bytes, sorted by codepoint):
        - codepoint: uint16_t (little-endian)
        - offset: uint32_t (little-endian, offset to bitmap data)
    
    Bitmap data:
        - Each glyph: (width * height + 7) // 8 bytes
        - Bits packed MSB first, row by row
"""

import struct
import argparse
import re
from pathlib import Path


def parse_bdf(bdf_path: str) -> tuple[dict, list[tuple[int, int, int, list[int]]]]:
    """
    解析 BDF 文件
    
    Returns:
        (properties, glyphs)
        properties: dict with font properties
        glyphs: list of (encoding, width, height, bitmap_rows)
    """
    properties = {}
    glyphs = []
    
    with open(bdf_path, 'r', encoding='latin-1') as f:
        lines = f.readlines()
    
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        
        # Parse properties
        if line.startswith('FONTBOUNDINGBOX '):
            parts = line.split()
            properties['fbb_width'] = int(parts[1])
            properties['fbb_height'] = int(parts[2])
            properties['fbb_x'] = int(parts[3])
            properties['fbb_y'] = int(parts[4])
        elif line.startswith('FONT_ASCENT '):
            properties['ascent'] = int(line.split()[1])
        elif line.startswith('FONT_DESCENT '):
            properties['descent'] = int(line.split()[1])
        elif line.startswith('CHARS '):
            properties['char_count'] = int(line.split()[1])
        
        # Parse glyph
        elif line.startswith('STARTCHAR '):
            encoding = None
            bbx_w, bbx_h, bbx_x, bbx_y = 0, 0, 0, 0
            dwidth = 0
            bitmap_rows = []
            
            i += 1
            while i < len(lines):
                gline = lines[i].strip()
                
                if gline.startswith('ENCODING '):
                    encoding = int(gline.split()[1])
                elif gline.startswith('DWIDTH '):
                    dwidth = int(gline.split()[1])
                elif gline.startswith('BBX '):
                    parts = gline.split()
                    bbx_w = int(parts[1])
                    bbx_h = int(parts[2])
                    bbx_x = int(parts[3])
                    bbx_y = int(parts[4])
                elif gline == 'BITMAP':
                    i += 1
                    while i < len(lines) and lines[i].strip() != 'ENDCHAR':
                        hex_str = lines[i].strip()
                        if hex_str:
                            row_val = int(hex_str, 16)
                            bitmap_rows.append(row_val)
                        i += 1
                    break
                
                i += 1
            
            if encoding is not None and encoding >= 0:
                # 存储原始 BDF 数据
                glyphs.append({
                    'encoding': encoding,
                    'dwidth': dwidth,
                    'bbx_w': bbx_w,
                    'bbx_h': bbx_h,
                    'bbx_x': bbx_x,
                    'bbx_y': bbx_y,
                    'bitmap': bitmap_rows
                })
        
        i += 1
    
    return properties, glyphs


def render_glyph_to_fixed_size(glyph: dict, target_width: int, target_height: int,
                                font_ascent: int, font_descent: int) -> bytes:
    """
    将 BDF 字形渲染到固定尺寸的位图
    
    BDF 坐标系：
    - (0, 0) 是基线左端
    - y 向上为正
    - BBX 定义字形边界框相对于原点的位置
    
    目标：将字形放在 target_width x target_height 的固定画布上
    """
    bbx_w = glyph['bbx_w']
    bbx_h = glyph['bbx_h']
    bbx_x = glyph['bbx_x']
    bbx_y = glyph['bbx_y']
    bitmap = glyph['bitmap']
    
    # 创建目标画布 (target_height 行，每行 target_width 位)
    canvas = [[0] * target_width for _ in range(target_height)]
    
    if bbx_w == 0 or bbx_h == 0 or not bitmap:
        # 空字符（如空格）
        pass
    else:
        # 计算字形在画布上的位置
        # BDF 的 y 坐标是从基线算起的，需要转换到像素坐标
        # 基线位置 = font_ascent（从顶部算起）
        baseline_y = font_ascent - 1  # 0-indexed
        
        # 字形顶部在画布上的 y 坐标
        glyph_top_y = baseline_y - (bbx_y + bbx_h - 1)
        
        # 字形左侧在画布上的 x 坐标
        glyph_left_x = bbx_x
        
        # 将 BDF 位图复制到画布
        for row_idx, row_val in enumerate(bitmap):
            canvas_y = glyph_top_y + row_idx
            if canvas_y < 0 or canvas_y >= target_height:
                continue
            
            # BDF 位图是从高位开始的
            # 需要根据 BBX 宽度正确解析
            for bit_idx in range(bbx_w):
                canvas_x = glyph_left_x + bit_idx
                if canvas_x < 0 or canvas_x >= target_width:
                    continue
                
                # 计算位图中对应的位
                # BDF 的位图是按字节存储的，MSB 在左边
                byte_width = (bbx_w + 7) // 8 * 8  # 对齐到字节边界
                bit_pos = byte_width - 1 - bit_idx
                
                if (row_val >> bit_pos) & 1:
                    canvas[canvas_y][canvas_x] = 1
    
    # 将画布转换为打包的字节
    bits = []
    for row in canvas:
        bits.extend(row)
    
    # 打包成字节 (MSB first)
    byte_count = (len(bits) + 7) // 8
    result = bytearray(byte_count)
    for i, bit in enumerate(bits):
        if bit:
            result[i // 8] |= (0x80 >> (i % 8))
    
    return bytes(result)


def create_fnt_file(bdf_path: str, fnt_path: str, target_width: int = None, target_height: int = None):
    """
    将 BDF 转换为 FNT 格式
    """
    print(f"解析 BDF 文件: {bdf_path}")
    properties, glyphs = parse_bdf(bdf_path)
    
    print(f"  字体边界框: {properties.get('fbb_width')}x{properties.get('fbb_height')}")
    print(f"  Ascent: {properties.get('ascent')}, Descent: {properties.get('descent')}")
    print(f"  字符数: {len(glyphs)}")
    
    # 确定目标尺寸
    font_ascent = properties.get('ascent', properties.get('fbb_height', 9))
    font_descent = properties.get('descent', 0)
    
    if target_width is None:
        target_width = properties.get('fbb_width', 9)
    if target_height is None:
        target_height = font_ascent + font_descent
    
    print(f"  目标尺寸: {target_width}x{target_height}")
    
    # 过滤有效字符 (codepoint <= 65535 for uint16_t)
    valid_glyphs = [g for g in glyphs if 0 < g['encoding'] <= 65535]
    valid_glyphs.sort(key=lambda g: g['encoding'])
    
    print(f"  有效字符数 (1-65535): {len(valid_glyphs)}")
    
    # 渲染所有字形
    print("渲染字形...")
    rendered = []
    for g in valid_glyphs:
        bitmap = render_glyph_to_fixed_size(g, target_width, target_height, font_ascent, font_descent)
        rendered.append((g['encoding'], bitmap))
    
    # 构建 FNT 文件
    bytes_per_glyph = (target_width * target_height + 7) // 8
    glyph_count = len(rendered)
    
    # Header: 16 bytes
    header = struct.pack('<4sBBBBII',
        b'TFNT',           # magic
        1,                 # version
        target_width,      # width
        target_height,     # height
        0,                 # flags
        glyph_count,       # glyph_count
        16                 # index_offset (right after header)
    )
    
    # Index: 6 bytes per entry (uint16 codepoint + uint32 offset)
    index_size = glyph_count * 6
    bitmap_start = 16 + index_size
    
    index_data = bytearray()
    bitmap_data = bytearray()
    
    for i, (codepoint, bitmap) in enumerate(rendered):
        offset = bitmap_start + i * bytes_per_glyph
        index_data += struct.pack('<HI', codepoint, offset)
        bitmap_data += bitmap
    
    # 写入文件
    with open(fnt_path, 'wb') as f:
        f.write(header)
        f.write(index_data)
        f.write(bitmap_data)
    
    file_size = 16 + len(index_data) + len(bitmap_data)
    print(f"\n生成完成: {fnt_path}")
    print(f"  文件大小: {file_size / 1024:.1f} KB")
    print(f"  字形数: {glyph_count}")
    print(f"  尺寸: {target_width}x{target_height}")
    print(f"  每字形字节: {bytes_per_glyph}")


def verify_glyph(bdf_path: str, codepoint: int, target_width: int = 9, target_height: int = 9):
    """
    验证单个字符的渲染结果
    """
    properties, glyphs = parse_bdf(bdf_path)
    
    font_ascent = properties.get('ascent', 9)
    font_descent = properties.get('descent', 0)
    
    if target_height is None:
        target_height = font_ascent + font_descent
    
    for g in glyphs:
        if g['encoding'] == codepoint:
            print(f"字符 U+{codepoint:04X}:")
            print(f"  BBX: {g['bbx_w']}x{g['bbx_h']} at ({g['bbx_x']}, {g['bbx_y']})")
            print(f"  DWidth: {g['dwidth']}")
            print(f"  原始位图行数: {len(g['bitmap'])}")
            
            bitmap = render_glyph_to_fixed_size(g, target_width, target_height, font_ascent, font_descent)
            
            print(f"\n渲染结果 ({target_width}x{target_height}):")
            for y in range(target_height):
                row = ""
                for x in range(target_width):
                    bit_idx = y * target_width + x
                    byte_idx = bit_idx // 8
                    bit_pos = 7 - (bit_idx % 8)
                    if bitmap[byte_idx] & (1 << bit_pos):
                        row += "██"
                    else:
                        row += "  "
                print(f"  {row}")
            return
    
    print(f"未找到字符 U+{codepoint:04X}")


def main():
    parser = argparse.ArgumentParser(description='BDF to FNT converter for TianShanOS')
    parser.add_argument('input', help='Input BDF file')
    parser.add_argument('output', nargs='?', help='Output FNT file')
    parser.add_argument('--width', '-W', type=int, help='Target glyph width')
    parser.add_argument('--height', '-H', type=int, help='Target glyph height')
    parser.add_argument('--verify', '-v', type=str, help='Verify a character (hex codepoint, e.g. 4E2D for 中)')
    
    args = parser.parse_args()
    
    if args.verify:
        codepoint = int(args.verify, 16)
        verify_glyph(args.input, codepoint, args.width or 9, args.height or 9)
    else:
        if not args.output:
            args.output = Path(args.input).with_suffix('.fnt')
        create_fnt_file(args.input, args.output, args.width, args.height)


if __name__ == '__main__':
    main()
