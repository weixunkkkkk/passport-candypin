#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."
test_dir="$(mktemp -d)"
trap 'rm -f "$test_dir/test_snake_model"; rmdir "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_snake_model.c main/snake_model.c main/snake_art.c main/snake_arcade.c \
    main/snake_bricks.c main/snake_games.c main/snake_pixels.c -lm -o "$test_dir/test_snake_model"
"$test_dir/test_snake_model"
