#!/usr/bin/env python2

"""
This script encodes a private key file from input parameter and stores it as header file for later use.

Also, it ouputs `<dest>.sha256` with sha256 hexdigest of used original key file for comparison.
"""
from __future__ import print_function

import os
import random
import string
import argparse
import hashlib

from itertools import cycle

TEMPLATE = '''
// THIS FILE IS GENERATED AUTOMATICALLY, DO NOT EDIT IT!

#include <smart_devices/libs/platform_key/platform_key.h>

#include <string>
#include <cstdint>
#include <cstring>

namespace {

const char* xorPhrase = "__XOR_PHRASE__";
int8_t key[] = { __KEY__ };

} // anonymous namespace

std::string quasar::getPlatformKey() {
    constexpr size_t keyLength = sizeof(key) / sizeof(key[0]);
    const size_t phraseLength = strlen(xorPhrase);

    std::string result;
    result.reserve(keyLength);

    for (size_t i = 0; i < keyLength; ++i) {
        result += char(key[i] ^ xorPhrase[i % phraseLength]);
    }

    return result;
}
'''


def main(path_to_private_key, path_to_result, path_to_digest):
    with open(path_to_private_key) as k:
        raw_key = k.read()

    xor_phrase = ''.join(random.choice(string.letters + string.digits) for _ in range(100))

    key = ', '.join(
        str(ord(key_letter) ^ ord(xor_letter))  # a single number for ascii-code of letter, like 73
        for (key_letter, xor_letter) in zip(raw_key, cycle(xor_phrase))
    )

    with open(path_to_result, 'wb') as result:
        result.write(
            TEMPLATE.replace('__XOR_PHRASE__', xor_phrase,).replace(
                '__KEY__',
                key,
            )
        )

    # FIXME: when tests start to be runnable for target platform make binaries output that checksum
    # see https://st.yandex-team.ru/QUASAR-2097
    with open(path_to_digest, 'wb') as result_sha256:
        result_sha256.write(hashlib.sha256(raw_key).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()

    parser.add_argument('--key', help='private key file to embed', required=True)
    parser.add_argument('--dest', help='destination file to write header to, defaults to a proper path', required=True)
    parser.add_argument(
        '--digest', help='destination file to write digest to, defaults to a proper path', required=True
    )

    args = parser.parse_args()

    if not os.path.exists(os.path.dirname(args.dest)):
        os.makedirs(os.path.dirname(args.dest))

    if 'QUASAR_PLATFORM_KEY_PATH' in os.environ:
        key = os.environ['QUASAR_PLATFORM_KEY_PATH']
    else:
        key = args.key

    main(key, args.dest, args.digest)

    print('Done, header written to', args.dest)
