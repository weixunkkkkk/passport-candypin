#include "snake_bricks.h"
#include "snake_art.h"

enum {
    JUMP_END_MS = 620, BUMP_END_MS = 470,
    ENTRY_X = SNAKE_BOARD_W - 1, EXIT_X = -BRICK_WIDTH,
    RUNNER_CENTER = BRICK_RUNNER_X + 8
};

static unsigned speed(const brick_game_t *game) {
    unsigned wave = game->index / 6;
    return 72u + (wave < 8 ? wave : 8u) * 6u;
}

int brick_game_left(const brick_game_t *game, uint32_t time) {
    return ENTRY_X - (int)((time - game->target_at) * speed(game) / 1000u);
}

bool brick_game_obstacle(const brick_game_t *game) {
    return game->index % 3 == 2;
}

static uint32_t crossing_time(const brick_game_t *game) {
    return game->target_at + (ENTRY_X + BRICK_WIDTH / 2 - RUNNER_CENTER) * 1000u / speed(game);
}

static bool aligned(const brick_game_t *game, uint32_t time) {
    int left = brick_game_left(game, time);
    return left < BRICK_RUNNER_X + 14 && left + BRICK_WIDTH > BRICK_RUNNER_X + 4;
}

int brick_game_height(const brick_game_t *game, uint32_t time) {
    if (!game->jumping || (int32_t)(time - game->jump_at) < 0) return 0;
    uint32_t jump = time - game->jump_at;
    if (jump < BRICK_CONTACT_MS) return (int)(BRICK_JUMP_HEIGHT * jump / BRICK_CONTACT_MS);
    uint32_t end = game->bumped ? BUMP_END_MS : JUMP_END_MS;
    return jump < end ? (int)(BRICK_JUMP_HEIGHT * (end - jump) / (end - BRICK_CONTACT_MS)) : 0;
}

brick_event_t brick_game_tick(brick_game_t *game, uint32_t time) {
    bool obstacle = brick_game_obstacle(game);
    brick_event_t event = BRICK_NONE;
    uint32_t contact = game->jump_at + BRICK_CONTACT_MS;
    if (!game->passed && obstacle) {
        uint32_t crossing = crossing_time(game);
        if (time >= crossing) {
            game->passed = true;
            if (brick_game_height(game, crossing) < 11) {
                game->crashed = true;
                game->feedback_at = time;
                event = BRICK_CRASH;
            }
        }
    } else if (!game->passed && game->jumping && time >= contact &&
               contact >= game->target_at && aligned(game, contact)) {
        game->passed = true;
        game->bumped = true;
        game->crashed = false;
        game->feedback_at = time;
        event = BRICK_COIN;
    }
    if (game->jumping && time - game->jump_at >= (game->bumped ? BUMP_END_MS : JUMP_END_MS))
        game->jumping = false;
    if (brick_game_left(game, time) <= EXIT_X) {
        game->target_at = time;
        ++game->index;
        game->passed = false;
    }
    return event;
}

void brick_game_jump(brick_game_t *game, uint32_t time) {
    if (game->jumping) return;
    game->jump_at = time;
    game->jumping = true;
    game->bumped = false;
}

bool brick_game_hint(const brick_game_t *game, uint32_t time) {
    if (game->jumping || game->passed) return false;
    if (!brick_game_obstacle(game)) return aligned(game, time + BRICK_CONTACT_MS);
    uint32_t lead = crossing_time(game) - time;
    return lead >= 115 && lead <= 365;
}

bool brick_relay_active(const brick_relay_t *relay, uint32_t now) {
    return relay && relay->pending && now - relay->at < BRICK_RELAY_MS;
}

bool brick_relay_observe(brick_relay_t *relay, const snake_sample_t *before,
                         const snake_sample_t *after, uint32_t now) {
    if (before->source == SNAKE_NONE || before->source != after->source ||
        (int32_t)(after->at - before->at) <= 0 || brick_relay_active(relay, now)) return false;
    int64_t total_delta = (int64_t)snake_total(after) - snake_total(before);
    if (total_delta < -1000 || total_delta > 1000) return false;
    int from = -1, to = -1;
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        const snake_port_t *old = &before->port[i], *next = &after->port[i];
        if (old->attached != next->attached || old->session != next->session) return false;
        int64_t delta = (int64_t)next->mw - old->mw;
        if (delta <= -5000) {
            if (from >= 0 || !old->attached || !old->mw || !next->mw) return false;
            from = (int)i;
        } else if (delta >= 5000) {
            if (to >= 0 || !old->attached || !old->mw || !next->mw) return false;
            to = (int)i;
        } else if (delta < -1000 || delta > 1000) return false;
    }
    if (from < 0 || to < 0) return false;
    *relay = (brick_relay_t){.at = now, .from = (uint8_t)from, .to = (uint8_t)to, .pending = true};
    return true;
}

snake_sample_t brick_replay_sample(unsigned step, uint32_t now) {
    snake_sample_t sample = {.source = SNAKE_DEMO, .at = now};
    sample.port[0] = (snake_port_t){step ? 10000 : 20000, 1, true};
    sample.port[3] = (snake_port_t){step ? 50000 : 40000, 1, true};
    return sample;
}
