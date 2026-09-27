#!/usr/bin/env bash
# SproutOS M1 - build a FAT32 disk image for QEMU -hda
# deps: sudo apt install -y dosfstools mtools
set -e
IMG=disk.img
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count=256 status=none
mkfs.vfat -F32 "$IMG" >/dev/null
mmd -i "$IMG" ::/docs
mmd -i "$IMG" ::/docs/sub
printf 'hello from the SproutOS file system!\nthis is a real file on a real FAT32 disk.\n' > /tmp/note.txt
mcopy -i "$IMG" /tmp/note.txt ::/note.txt
printf 'SproutOS readme\nsecond line\nthird line\n' > /tmp/readme.txt
mcopy -i "$IMG" /tmp/readme.txt ::/docs/readme.txt
printf 'nested file inside docs/sub\n' > /tmp/nested.txt
mcopy -i "$IMG" /tmp/nested.txt ::/docs/sub/nested.txt

# 桌面壁纸：把项目目录 wallpaper/background.bmp 拷进镜像 /wallpaper/bg.bmp
# 注意：必须用 8.3 短文件名 + MTOOLS_NO_VFAT=1，否则内核 FAT32 驱动找不到
mmd -i "$IMG" ::/wallpaper 2>/dev/null || true
WP_SRC="$(ls wallpaper/background*.bmp 2>/dev/null | head -n1 | sed 's/[[:space:]]*$//')"
if [ -n "$WP_SRC" ] && [ -f "$WP_SRC" ]; then
    MTOOLS_NO_VFAT=1 mcopy -i "$IMG" "$WP_SRC" ::/wallpaper/BG.BMP
    echo "wallpaper copied -> /wallpaper/BG.BMP (from $WP_SRC)"
else
    echo "warning: wallpaper/background.bmp not found, desktop will use built-in default"
fi

# 浏览器资源：把 browser/ 下的网页/图片/搜索配置拷进镜像 /browser/
# FAT32 是 8.3 / 大小写不敏感，所以：
#   home.html     -> HOME.HTML
#   test.bmp      -> TEST.BMP
#   search_config.txt -> SRCHCFG.TXT  (浏览器按 /browser/srchcfg.txt 读取)
# 关键：必须用 MTOOLS_NO_VFAT=1 禁止 mtools 创建 LFN 长文件名条目，
#       否则 home.html 会被存为 LFN + 8.3 别名 HOME~1.HTM，
#       导致浏览器 fat32_open_in_dir 查找 "home.html"(→HOME.HTML) 匹配不到磁盘上的 HOME~1.HTM。
mmd -i "$IMG" ::/browser 2>/dev/null || true
BROWSER_COPIED=0
if [ -f browser/home.html ]; then
    MTOOLS_NO_VFAT=1 mcopy -i "$IMG" browser/home.html "::/browser/HOME.HTML"
    # 验证拷贝成功：用 mdir 列目录确认 HOME.HTML 存在（不是 HOME~1.HTM）
    if mdir -i "$IMG" -/ ::/browser/ 2>/dev/null | grep -q "HOME\.HTML"; then
        echo "browser home page copied -> /browser/home.html (8.3: HOME.HTML)"
        BROWSER_COPIED=1
    else
        echo "ERROR: browser/home.html copy FAILED or wrong name!"
        mdir -i "$IMG" -/ ::/browser/
    fi
else
    echo "warning: browser/home.html not found"
fi
if [ -f browser/test.bmp ]; then
    MTOOLS_NO_VFAT=1 mcopy -i "$IMG" browser/test.bmp "::/browser/TEST.BMP"
    echo "browser test image copied -> /browser/test.bmp"
else
    echo "warning: browser/test.bmp not found (test_page.html <img> will be blank)"
fi
if [ -f browser/search_config.txt ]; then
    MTOOLS_NO_VFAT=1 mcopy -i "$IMG" browser/search_config.txt "::/browser/SRCHCFG.TXT"
    echo "browser search config copied -> /browser/srchcfg.txt (8.3: SRCHCFG.TXT)"
else
    echo "warning: browser/search_config.txt not found (browser will use default engine 0)"
fi

# 中文字体：把 fonts/SproutOS-YShiNewHeiGGJRegular.ttf 拷进镜像 /YSHI.TTF
# FAT32 是 8.3 / 大小写不敏感，所以文件名必须短且无大小写混用。
if [ -f fonts/SproutOS-YShiNewHeiGGJRegular.ttf ]; then
    mcopy -i "$IMG" fonts/SproutOS-YShiNewHeiGGJRegular.ttf ::/YSHI.TTF
    echo "TTF font copied -> /YSHI.TTF (runtime Chinese font for stb_truetype)"
else
    echo "warning: fonts/SproutOS-YShiNewHeiGGJRegular.ttf not found, GUI will fall back to baked bitmap font"
fi

# ===== Seed 包 (M5)：Terra 源码 → terrac 编译 → .seed → The Greenhouse/APPS =====
# 编译器是 scripts/terrac.py（python3），源码在 scripts/seed-examples/*.terra
# 目录约定（FAT32 8.3）：
#   /GREENHOU  The Greenhouse（温室：所有可用种子/.seed 包）
#   /APPS      花园（已播种/已安装到系统的包）
#   /CACHE     缓存/依赖（prune 清理目标）
# 终端命令：planter / terra run / plant / weed / water / rain / garden / seek / sniff / prune
if command -v python3 >/dev/null 2>&1; then
    # 建目录（mtools 默认创建 LFN 条目；用 MTOOLS_NO_VFAT=1 仅在写文件时禁用，
    # 这里 mmd 建目录无所谓，OS 的 LFN 解码也能正确识别）
    mmd -i "$IMG" ::/GREENHOU 2>/dev/null || true
    mmd -i "$IMG" ::/APPS 2>/dev/null || true
    mmd -i "$IMG" ::/CACHE 2>/dev/null || true
    SEED_TMP=$(mktemp -d)
    for SRC in scripts/seed-examples/*.terra; do
        [ -f "$SRC" ] || continue
        BASE=$(basename "$SRC" .terra)
        # 8.3 名：截断到 8 字符大写
        SHORT=$(echo "$BASE" | cut -c1-8 | tr '[:lower:]' '[:upper:]')
        if python3 scripts/terrac.py "$SRC" -o "$SEED_TMP/$SHORT.seed" >/dev/null 2>&1; then
            # 温室：所有可用种子（.SEED，4字符扩展名，长文件名）
            MTOOLS_NO_VFAT=1 mcopy -i "$IMG" "$SEED_TMP/$SHORT.seed" "::/GREENHOU/$SHORT.SEED"
            # 花园：预播种（已安装），.SPT（3字符扩展名，纯短文件名，避免长文件名读取问题）
            MTOOLS_NO_VFAT=1 mcopy -i "$IMG" "$SEED_TMP/$SHORT.seed" "::/APPS/$SHORT.SPT"
            echo "seed package -> /GREENHOU/$SHORT.SEED + /APPS/$SHORT.SPT (from $BASE.terra)"
        else
            echo "warning: failed to compile $SRC"
        fi
    done
    rm -rf "$SEED_TMP"
else
    echo "warning: python3 not found, skipping .seed package compilation"
fi

echo "disk.img created (256MB FAT32). Now run: make run"
