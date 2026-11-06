#include "snake_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint32_t STEP_MS = 220;

static const snake_point_t track[SNAKE_TRACK] = {
    {1,8},{1,7},{1,6},{1,5},{1,4},{1,3},{1,2},
    {1,1},{2,1},{3,1},{4,1},{5,1},{6,1},{7,1},{8,1},{9,1},{10,1},{11,1},{12,1},{13,1},{14,1},
    {14,2},{14,3},{13,3},{12,3},{11,3},{10,3},{9,3},{8,3},{7,3},{6,3},{5,3},
    {5,4},{5,5},{5,6},{6,6},{7,6},{8,6},{9,6},{10,6},{11,6},{12,6},{13,6},{14,6},
    {14,7},{14,8},{13,8},{12,8},{11,8},{10,8},{9,8},{8,8},{7,8},{6,8},{5,8},{4,8},{3,8},{2,8}
};

snake_point_t snake_track(int index) {
    int wrapped = (index % SNAKE_TRACK + SNAKE_TRACK) % SNAKE_TRACK;
    snake_point_t point = track[wrapped];
    point.x = point.x * SNAKE_CELL + SNAKE_CELL / 2;
    point.y = point.y * SNAKE_CELL + SNAKE_CELL / 2;
    return point;
}

bool snake_port_idle(const snake_port_t *p) {
    return !p->attached && p->mw == 0 && p->session == 0;
}

uint32_t snake_total(const snake_sample_t *sample) {
    uint32_t total = 0;
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) total += sample->port[i].mw;
    return total;
}

void snake_power_text(char text[12], uint32_t mw) {
    if (mw > 0 && mw < 50) {
        strcpy(text, "<0.1");
        return;
    }
    uint32_t tenths = (mw + 50) / 100;
    snprintf(text, 12, "%lu.%lu", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}

static void targets(const snake_sample_t *sample, float out[SNAKE_PORTS]) {
    float total = (float)snake_total(sample);
    float scale = SNAKE_CELL / fmaxf(5000.0f, total / 40.0f);
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) out[i] = sample->port[i].mw * scale;
}

void snake_reset(snake_model_t *m) {
    memset(m, 0, sizeof *m);
    m->head = 22;
}

snake_freshness_t snake_freshness(const snake_model_t *m, uint32_t now) {
    if (!m->has_sample) return SNAKE_EMPTY;
    if (m->sample.source == SNAKE_DEMO) return SNAKE_FRESH;
    uint32_t age = now - m->sample.at;
    if ((int32_t)age < 0) age = 0;
    uint32_t period = m->sample.interval_ms;
    if (!period) period = m->sample.source == SNAKE_MCP ? 3000u : 1000u;
    uint32_t wait = 3u * period, lost = 8u * period;
    if (age >= (lost < 10000u ? 10000u : lost)) return SNAKE_LOST;
    return age >= (wait < 4000u ? 4000u : wait) ? SNAKE_WAIT : SNAKE_FRESH;
}

static void advance_length(snake_model_t *m, uint32_t now) {
    if (!m->change_ms) return;
    float t = (float)(now - m->change_at) / m->change_ms;
    if (t >= 1.0f) {
        memcpy(m->body, m->to, sizeof m->body);
        m->change_ms = 0;
        return;
    }
    float smooth = t * t * (3.0f - 2.0f * t);
    for (unsigned i = 0; i < SNAKE_PORTS; ++i)
        m->body[i] = m->from[i] + (m->to[i] - m->from[i]) * smooth;
}

static void morph(snake_model_t *m, uint32_t now, bool recovering) {
    float next[SNAKE_PORTS];
    targets(&m->sample, next);
    float delta_total = 0, max_change = 0;
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        float delta = next[i] - m->body[i];
        delta_total += delta;
        max_change = fmaxf(max_change, fabsf(delta));
    }
    if (m->change_ms && memcmp(next, m->to, sizeof next) == 0) return;
    if (max_change < 0.001f) {
        memcpy(m->body, next, sizeof m->body);
        m->change_ms = 0;
        return;
    }
    memcpy(m->from, m->body, sizeof m->from);
    memcpy(m->to, next, sizeof m->to);
    m->change_at = now;
    m->change_ms = !recovering && fmaxf(fabsf(delta_total), max_change) > 8 * SNAKE_CELL ? 500 : 350;
}

void snake_accept(snake_model_t *m, const snake_sample_t *sample, uint32_t now) {
    if (m->has_sample && m->sample.source == sample->source && m->sample.at == sample->at) return;
    bool first = !m->has_sample;
    bool recovering = !first && (m->held || snake_freshness(m, now) != SNAKE_FRESH ||
                                 m->sample.source != sample->source);
    if (!first && !recovering) advance_length(m, now);
    if (first || recovering) {
        m->pellet_ports = 0;
        m->sparkling = false;
        m->change_ms = 0;
        m->moving = false;
        m->zero_streak = 0;
        for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
            m->port[i] = (snake_port_anim_t){.idle_streak = first && snake_port_idle(&sample->port[i]) ? 2 : 0};
        }
    } else {
        uint8_t previous_pellet = m->pellet_ports;
        for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
            snake_port_anim_t *a = &m->port[i];
            const snake_port_t *p = &sample->port[i];
            uint8_t bit = (uint8_t)(1u << i);
            if (!p->attached || !p->mw) m->pellet_ports &= (uint8_t)~bit;
            if (p->attached) {
                if (a->idle_streak == 2) {
                    a->armed = true;
                    a->pulse_at = now;
                    a->pulsing = true;
                }
                if (p->mw && a->armed) {
                    m->pellet_ports |= bit;
                    a->armed = false;
                    a->pulse_at = now;
                    a->pulsing = true;
                }
                a->idle_streak = 0;
            } else if (snake_port_idle(p)) {
                if (a->idle_streak < 2) ++a->idle_streak;
                if (a->idle_streak == 2) a->armed = false;
            } else {
                a->idle_streak = 0;
            }
        }
        if (m->pellet_ports && !(previous_pellet & m->pellet_ports))
            m->pellet = (uint8_t)((m->head + 2) % SNAKE_TRACK);
    }
    m->sample = *sample;
    m->has_sample = true;
    m->held = false;
    if (snake_total(sample)) m->zero_streak = 0;
    else if (m->zero_streak < 2) ++m->zero_streak;
    if (first) targets(sample, m->body);
    else if (m->pellet_ports) m->change_ms = 0;
    else morph(m, now, recovering);
}

void snake_tick(snake_model_t *m, uint32_t now) {
    if (snake_freshness(m, now) != SNAKE_FRESH) {
        m->held = m->has_sample;
        m->moving = false;
        m->change_ms = 0;
        m->pellet_ports = 0;
        m->sparkling = false;
        return;
    }
    advance_length(m, now);
    if (m->sparkling && now - m->spark_at >= 120) m->sparkling = false;
    for (unsigned i = 0; i < SNAKE_PORTS; ++i)
        if (m->port[i].pulsing && now - m->port[i].pulse_at >= 620) m->port[i].pulsing = false;
    bool has_length = m->change_ms || snake_total(&m->sample) || m->pellet_ports;
    if (!has_length) {
        m->moving = false;
        return;
    }
    if (!m->moving) {
        m->moving = true;
        m->step_at = now;
    } else if (now - m->step_at >= STEP_MS) {
        m->head = (uint8_t)((m->head + 1) % SNAKE_TRACK);
        m->step_at = now - (now - m->step_at) % STEP_MS;
        if (m->pellet_ports && m->head == m->pellet) {
            m->spark_color = (uint8_t)__builtin_ctz(m->pellet_ports);
            m->spark_at = now;
            m->sparkling = true;
            m->pellet_ports = 0;
            morph(m, now, false);
        }
    }
}
