import argparse
import os
from PIL import Image


def parse_args():
    parser = argparse.ArgumentParser(description="Creates segment clock animation base on png sequence")
    parser.add_argument('seq_path', type=str, help='path to png sequence dir')
    return parser.parse_args()


def prettify(val):
    if val < 10:
        return str(val) + "  "
    if val < 100:
        return str(val) + " "
    return str(val)


def main(args):
    pixels = (
        ((15, 5), (25, 15), (25, 35), (15, 45), (5, 35), (5, 15), (15, 25)),
        ((45, 5), (55, 15), (55, 35), (45, 45), (35, 35), (35, 15), (45, 25)),
        ((100, 5), (110, 15), (110, 35), (100, 45), (90, 35), (90, 15), (100, 25)),
        ((130, 5), (140, 15), (140, 35), (130, 45), (120, 35), (120, 15), (130, 25)),
        ((72, 17), (72, 30), (72, 23), (153, 3)),
    )

    directory = os.path.abspath(args.seq_path) + '/'
    ledname = os.path.basename(os.path.normpath(directory)) + ".scled"
    files = os.listdir(directory)
    images = filter(lambda x: x.endswith('.png'), files)

    with open(ledname, "w") as text_file:
        for image in sorted(images):
            file = directory + image

            img = Image.open(file)
            greyscale_im = img.convert('LA')

            values = list()

            for group in pixels:
                for pixel in group:
                    w, _ = greyscale_im.getpixel(pixel)
                    values.append(prettify(w))
                values.append('|')

            text_file.write(" ".join(values) + " 40\n")


if __name__ == "__main__":
    main(parse_args())
