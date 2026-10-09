"""把已获授权的SVG生成嵌入式手柄图；普通构建使用已生成头文件，不依赖Qt/Pillow。"""
from pathlib import Path
import os
import hashlib,sys
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')

def main():
    sys.stdout.reconfigure(encoding='utf-8')
    root=Path(__file__).resolve().parents[1]
    source=root/'assets/360_controller_clean.svg';header=root/'src/Runtime/ControllerArtData.h'
    checksum=hashlib.sha256(source.read_bytes()).hexdigest()
    if '--check' in sys.argv:
        if f'#define CONTROLLER_ART_SVG_SHA256 "{checksum}"' not in header.read_text(encoding='utf-8'):
            raise RuntimeError('手柄SVG与嵌入资源不同步，请运行generate_controller_art.py重新生成')
        print('手柄SVG与嵌入资源同步通过');return
    from PySide6.QtGui import QGuiApplication,QImage,QPainter,QColor
    from PySide6.QtSvg import QSvgRenderer
    from PIL import Image,ImageChops
    root=Path(__file__).resolve().parents[1]
    app=QGuiApplication.instance() or QGuiApplication([])
    renderer=QSvgRenderer(str(root/'assets/360_controller_clean.svg'))
    if not renderer.isValid():raise RuntimeError('手柄SVG无效')
    # 按SVG自身的绘图区比例渲染，换图时不沿用旧图的宽高，避免压扁手柄。
    bounds_svg=renderer.viewBoxF()
    if bounds_svg.width()<=0 or bounds_svg.height()<=0:raise RuntimeError('手柄SVG绘图区无效')
    width=1024;height=max(1,round(width*bounds_svg.height()/bounds_svg.width()))
    image=QImage(width,height,QImage.Format.Format_RGB32);image.fill(QColor(20,18,14))
    painter=QPainter(image);renderer.render(painter);painter.end()
    rgb=Image.frombytes('RGB',(width,height),image.constBits().tobytes(),'raw','BGRX')
    bounds=ImageChops.difference(rgb,Image.new('RGB',rgb.size,(20,18,14))).getbbox()
    if not bounds:raise RuntimeError('手柄SVG没有可见内容')
    left,top,right,bottom=bounds;left=max(0,left-12);top=max(0,top-12);right=min(width,right+12);bottom=min(height,bottom+12)
    rgb=rgb.crop((left,top,right,bottom))
    height=round(rgb.height*512/rgb.width);rgb=rgb.resize((512,height),Image.Resampling.LANCZOS)
    data=rgb.tobytes('raw','BGRX')
    lines=['/* 由tools/generate_controller_art.py生成；BGRX顶向下像素，嵌入ASI，无外部图片依赖。 */',
           f'#define CONTROLLER_ART_SVG_SHA256 "{checksum}"','#define CONTROLLER_ART_WIDTH 512',f'#define CONTROLLER_ART_HEIGHT {height}','static const unsigned char controller_art_pixels[]={']
    for i in range(0,len(data),32):lines.append(','.join('0x%02X'%value for value in data[i:i+32])+',')
    lines.extend(['};','']);(root/'src/Runtime/ControllerArtData.h').write_bytes('\r\n'.join(lines).encode('utf-8'))
    print(f'手柄显示资源已生成：512×{height}；原SVG未改')
if __name__=='__main__':main()
