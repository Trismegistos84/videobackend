#!/usr/bin/python3

# dependencies:
# ffmpeg binary visiable on path -- tested on version 4.2.2
# python imaging library -- tested on version 7.2

import subprocess
import tempfile
import os
import os.path
from PIL import Image


infile = 'jameiswinstonpas.mp4'
tempdirbase = '/dev/shm' # set some accessible and existing directory


def extract_frames_to_directory(infile, outdir):
    frame_file_pattern = os.path.join(outdir, "%d.png");
    cmd = ["ffmpeg", "-i", infile, "-vf", r"select=eq(pict_type\,I)", "-vsync", "vfr", frame_file_pattern]
    subprocess.check_call(cmd)


def read_frames_sequence(framesdir):
    frames = os.listdir(framesdir)
    get_frame_number = lambda frame: int(frame.split('.')[0])
    frames.sort(key=get_frame_number)
    frames = [os.path.join(framesdir, frame) for frame in frames]
    return [Image.open(frame) for frame in frames]
    
    
def concatenate_frames(frames, outfile):
    frames_count = len(frames)

    if frames_count == 0:
        raise Exception("frames list may not be empty")

    (frame_width, frame_height) = frames[0].size
    outimage = Image.new("RGB", (frame_width * frames_count, frame_height))
    for i in range(frames_count):
        outimage.paste(frames[i], (i*frame_width, 0))
    
    outimage.save(outfile)


tempdir = tempfile.TemporaryDirectory(suffix=None, prefix='frame_extractor_', dir=tempdirbase)

extract_frames_to_directory(infile, tempdir.name)
frames = read_frames_sequence(tempdir.name)
concatenate_frames(frames, 'frames.png')


