# pi-freetype

在树莓派zero/3B上使用freetype绘制中文。

![fb截图](https://github.com/mumumusuc/pi-freetype/blob/master/images/fb1.png)
```
# cat /dev/fb1 > fb1.raw
# ffmpeg -vcodec rawvideo -f rawvideo -pix_fmt gray8 -s 256X64 -i fb1.raw -f image2 -vcodec png fb%d.png
```

1. 编译[freetype](https://www.freetype.org/download.html)
	zero上编译freetype过于折磨，建议在Ubuntu上交叉编译。

2. 使用了哪些驱动？


