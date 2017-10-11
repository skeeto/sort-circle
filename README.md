# Sorting Animations

<p align="center">
  <a href="http://nullprogram.com/video/?v=sort-circle">
    <img alt="" title="Play video" src="https://i.imgur.com/PqHLUVm.png"/>
  </a>
</p>

This C program produces videos for various sorting algorithms. Points
are ordered by hue, and the distance from the center indicates a point's
distance from its correct position. The original idea for this animation
[comes from w0rthy][orig].

To create a video, pipe standard output into a video encoder:

    $ ./sort | x264 --fps 60 -o video.mp4 /dev/stdin

Some media players can play raw video from standard input as well:

    $ ./sort | mpv --no-correct-pts --fps=60 -

Others need help from [mjpegtools][mj] (`ppmtoy4m`):

    $ ./sort | ppmtoy4m -F60:1 | vlc -

The program's output is [a bunch of concatenated][how] images, one per
frame, in [PPM format][ppm].

[how]: http://nullprogram.com/blog/2017/07/02/
[mj]: http://mjpeg.sourceforge.net/
[orig]: https://www.youtube.com/watch?v=sYd_-pAfbBw
[ppm]: https://en.wikipedia.org/wiki/Netpbm_format
