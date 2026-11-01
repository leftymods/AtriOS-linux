import argparse
from argparse import RawTextHelpFormatter
import os


def ind_for_symb(ch):
    if ch == ':':
        return [0, 1]
    elif ch == '-':
        return [2]
    elif ch == '+':
        return [0, 1, 2]
    elif ch == ' ':
        return []
    else:
        raise Exception('Unexpected symbol: ' + ch)


def ind_for_digit(ch):
    if ch == '-':
        return [6]
    elif ch == ' ':
        return []
    elif ch == '0':
        return [0, 1, 2, 3, 4, 5]
    elif ch == '1':
        return [1, 2]
    elif ch == '2':
        return [0, 1, 3, 4, 6]
    elif ch == '3':
        return [0, 1, 2, 3, 6]
    elif ch == '4':
        return [1, 2, 5, 6]
    elif ch == '5':
        return [0, 2, 3, 5, 6]
    elif ch == '6':
        return [0, 2, 3, 4, 5, 6]
    elif ch == '7':
        return [0, 1, 2]
    elif ch == '8':
        return [0, 1, 2, 3, 4, 5, 6]
    elif ch == '9':
        return [0, 1, 2, 3, 5, 6]
    else:
        raise Exception('Unexpected symbol: ' + ch)


def prettify(val):
    if val < 10:
        return str(val) + "  "
    if val < 100:
        return str(val) + " "
    return str(val)


brightness_steps = [0, 43, 85, 128, 170, 213, 255]


def animate_symbol(symbol_ind, start_ind, end_ind):
    frames_parts = []
    for i in range(3):
        if i not in symbol_ind:
            frames_parts.append([0 for j in range(end_ind)])
        else:
            tmp = (
                [0 for j in range(start_ind)]
                + brightness_steps
                + [255 for j in range(end_ind - start_ind - len(brightness_steps))]
            )
            frames_parts.append(tmp)

    return frames_parts


def animate_digit_with_order(digit_inds, start_ind, end_ind, order):
    columns = []
    for i in range(7):
        if i not in digit_inds:
            columns.append([0 for j in range(end_ind)])
        else:
            off = start_ind + (0 if order[i] == 0 else order[i] * 3)
            on = end_ind - off - len(brightness_steps)
            tmp = [0 for j in range(off)] + brightness_steps + [255 for j in range(on)]
            assert len(tmp) == end_ind
            columns.append(tmp)
    return columns


def animate_digit_slide(digit_inds, start_ind, end_ind):
    order = {1: 0, 2: 0, 0: 1, 6: 1, 3: 1, 5: 2, 4: 2}
    return animate_digit_with_order(digit_inds, start_ind, end_ind, order)


def animate_gidit_to_dot(digit_inds, start_ind, end_ind):
    order = {4: 2, 3: 2, 5: 1, 6: 1, 2: 1, 0: 0, 1: 0}
    columns = animate_digit_with_order(digit_inds, start_ind, end_ind, order)
    for col in columns:
        col.reverse()
    return columns


def animate_degree_dot():
    fade_in = [i * 16 for i in range(16)]
    fade_in.append(255)
    fade_out = fade_in[::-1]
    column = [0 for i in range(0, 81)] + fade_in + fade_out + fade_in + fade_out
    return column


def generate_alarm_frames(time_str):
    symb = []
    digits = []
    for v in time_str:
        if v == ':':
            symb = ind_for_symb(v)
        else:
            digits.append(ind_for_digit(v))

    if len(symb) == 0:
        symb = ind_for_symb(':')

    columns = []
    end = 43
    for i, digit in enumerate(digits):
        start = 30
        if i == 1:
            start = 21
        if i == 2:
            start = 9
        if i == 3:
            start = 0
        columns += animate_digit_slide(digit, start, end)

    # symbols without degree
    symbols = animate_symbol(symb, 18, end)
    columns += symbols

    # first 3 digits just slide back
    for i in range(7 * 3):
        rev = reversed(columns[i])
        columns[i] += rev

    # add animation of the last digit
    last_digit_to_dot = animate_gidit_to_dot(digits[3], 0, end)
    for i in range(21, 28):
        columns[i] += last_digit_to_dot[i - 21]

    # sign just slides back as well
    for i in range(28, 31):
        rev = reversed(columns[i])
        columns[i] += rev

    degree_col = animate_degree_dot()
    zeros = [0 for i in range(len(degree_col) - 2 * end)]
    for col in columns:
        col += zeros

    columns.append(degree_col)
    columns.append([20 if i != end - 1 else 2100 for i in range(len(degree_col))])

    return [[columns[j][i] for j in range(len(columns))] for i in range(len(columns[0]))]


def generate_temp_frames(temp_str):
    digits = []
    columns = []
    symb = []
    while len(temp_str) != 5:
        temp_str = " " + temp_str
    for i, c in enumerate(temp_str):
        if i == 2:
            symb = ind_for_symb(c)
        else:
            digits.append(ind_for_digit(c))

    end = 29
    start = 0
    for i, digit in enumerate(digits):
        if i == 2:
            start = 12
        if i == 3:
            start = 3
        columns += animate_digit_slide(digit, start, end)
    symbols = animate_symbol(symb, 21, end)
    # animate degree
    symbols.append(brightness_steps + [255 for j in range(end - len(brightness_steps))])
    columns += symbols

    for col in columns:
        rev = reversed(col)
        col += rev

    columns.append([20 if i != end - 1 else 2000 for i in range(end * 2)])

    return [[columns[j][i] for j in range(len(columns))] for i in range(len(columns[0]))]


def main():
    parser = argparse.ArgumentParser(
        description="Creates alarm or temperature animations", formatter_class=RawTextHelpFormatter
    )
    parser.add_argument(
        '-a',
        '--alarm',
        type=str,
        help='''Alarm pattern.
Can be with or without ':' symbol
Input example:
    22:35
    0300''',
    )
    parser.add_argument(
        '-t',
        '--temp',
        type=str,
        help='''Temperature pattern.
Temperature below 0 should start with '-'
Temperature above 0 is drawn without '+'
Degree symbol will be added automatically
Input examples:
    5
    -21
    0''',
    )
    args = parser.parse_args()

    ledname = ''
    frames = []
    if args.alarm:
        frames = generate_alarm_frames(args.alarm)
        ledname = args.alarm + '.scled'
    elif args.temp:
        frames = generate_temp_frames(args.temp)
        ledname = args.temp + '.scled'
    else:
        parser.print_help()
        return

    with open(ledname, "w") as text_file:
        for frame in frames:
            values = list()
            for i, val in enumerate(frame):
                values.append(prettify(val))
                if i in [6, 13, 20, 27, 31]:
                    values.append('|')
            text_file.write(" ".join(values) + "\n")
        print(f"Generated led: {ledname}")


if __name__ == "__main__":
    main()
