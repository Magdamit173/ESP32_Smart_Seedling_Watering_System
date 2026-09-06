#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <Wire.h>
#include <RTClib.h>

#define DHT_PIN 4
#define WATER_PIN 5
#define DHT_TYPE DHT22
#define RTC_SDA 6
#define RTC_SCL 7

const char* WIFI_SSID = "ESP32_Smart_Seedling_Waterer";
const uint8_t MAX_EVENTS = 10;
const uint32_t MAX_DURATION_SEC = 1800;
const uint32_t SENSOR_INTERVAL_MS = 10000;
const uint32_t CONFIG_MAGIC = 0x53454544;

WebServer server(80);
DHT dht(DHT_PIN, DHT_TYPE);
Preferences prefs;
RTC_DS3231 rtc;
bool rtcAvailable = false;

struct ConditionConfig {
    bool enabled;
    uint8_t sensor;
    uint8_t op;
    float threshold;
};

struct EventConfig {
    bool enabled;
    uint8_t type;
    char time[6];
    uint32_t durationSec;
    ConditionConfig first;
    ConditionConfig second;
    bool secondEnabled;
    uint8_t logic;
    uint32_t persistenceSec;
    uint32_t cooldownSec;
};

struct DeviceConfig {
    uint32_t magic;
    bool automationEnabled;
    uint32_t manualDurationSec;
    uint8_t eventCount;
    EventConfig events[MAX_EVENTS];
};

DeviceConfig config;

int64_t rtcLastScheduleDay[MAX_EVENTS];
int64_t rtcConditionSince[MAX_EVENTS];
int64_t rtcLastConditionTrigger[MAX_EVENTS];
bool rtcStateInitialized = false;

float currentTemperature = NAN;
float currentHumidity = NAN;
bool waterState = false;
uint32_t waterStartMs = 0;
uint32_t waterDurationMs = 0;
String lastReason = "None";
uint32_t lastSensorReadMs = 0;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Smart Seedling Waterer</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <header class="site-header">
        <div class="header-content">
            <div>
                <h1 class="site-title">ESP32 Smart Seedling Waterer</h1>
                <p class="site-subtitle">Rule based watering controller</p>
            </div>
            <div class="connection-status">
                <span class="connection-dot" id="connection-dot" data-state="offline"></span>
                <span id="connection-label">Offline</span>
            </div>
        </div>
    </header>

    <main class="app">
        <section class="toolbar panel">
            <div class="device-controls">
                <label class="field-label" for="device-address">ESP32 Address</label>
                <input class="text-input" id="device-address" value="http://192.168.4.1" autocomplete="off" spellcheck="false">
                <button class="button button-primary" type="button" data-action="connect">Connect</button>
                <button class="button button-secondary" type="button" data-action="disconnect">Disconnect</button>
            </div>
            <div class="program-controls">
                <button class="button button-success" type="button" data-action="run">Enable Automation</button>
                <button class="button button-danger" type="button" data-action="stop">Stop Automation</button>
                <button class="button button-primary" type="button" data-action="save">Save to ESP32</button>
            </div>
        </section>

        <section class="status-grid">
            <article class="panel status-card">
                <span class="status-card-label">Device Time</span>
                <strong class="status-card-value" id="status-time">--:--:--</strong>
                <span class="status-card-note" id="status-clock">Clock not synced</span>
            </article>
            <article class="panel status-card">
                <span class="status-card-label">Temperature</span>
                <strong class="status-card-value" id="status-temperature">--</strong>
                <span class="status-card-note">DHT22 / AM2302</span>
            </article>
            <article class="panel status-card">
                <span class="status-card-label">Humidity</span>
                <strong class="status-card-value" id="status-humidity">--</strong>
                <span class="status-card-note">Relative humidity</span>
            </article>
            <article class="panel status-card">
                <span class="status-card-label">SW_WATER</span>
                <strong class="status-card-value" id="status-water">OFF</strong>
                <span class="status-card-note" id="status-water-time">Ready</span>
            </article>
        </section>

        <section class="content-grid">
            <div class="main-column">
                        <section class="panel section-card">
                    <div class="section-heading">
                        <div>
                            <h2>Scheduled Watering</h2>
                            <p>Water every day at a selected time for a selected duration.</p>
                        </div>
                        <button class="button button-primary" type="button" data-action="add-schedule">Add Time</button>
                    </div>
                    <div class="event-list" id="schedule-list"></div>
                    <div class="empty-message" id="schedule-empty">No scheduled watering events.</div>
                </section>

                <section class="panel section-card">
                    <div class="section-heading">
                        <div>
                            <h2>Temperature / Humidity Rules</h2>
                            <p>Water when a condition stays true for a required time. Cooldown prevents repeated watering.</p>
                        </div>
                        <button class="button button-primary" type="button" data-action="add-condition">Add Rule</button>
                    </div>
                    <div class="event-list" id="condition-list"></div>
                    <div class="empty-message" id="condition-empty">No environmental watering rules.</div>
                </section>
            </div>

            <aside class="side-column">
                <section class="panel section-card">
                    <div class="section-heading compact">
                        <div>
                            <h2>Manual Watering</h2>
                            <p>Wireless manual watering from this page.</p>
                        </div>
                    </div>
                    <label class="field-label" for="manual-duration">Duration</label>
                    <div class="inline-field">
                        <input class="text-input" id="manual-duration" type="number" min="1" max="1800" value="30">
                        <select class="select-input" id="manual-unit">
                            <option value="seconds">seconds</option>
                            <option value="minutes" selected>minutes</option>
                        </select>
                    </div>
                    <button class="button button-success button-large" type="button" data-action="manual-water">WATER NOW</button>
                    <button class="button button-danger button-large" type="button" data-action="force-stop">FORCE STOP WATERING</button>
                    <p class="safety-note">Hard limit: <strong>30 minutes</strong> maximum watering time.</p>
                </section>

                <section class="panel section-card">
                    <div class="section-heading compact">
                        <div>
                            <h2>Device</h2>
                            <p>Local controller status.</p>
                        </div>
                    </div>
                    <div class="device-detail"><span>Automation</span><strong id="device-automation">OFF</strong></div>
                    <div class="device-detail"><span>Last Action</span><strong id="device-reason">None</strong></div>
                    <div class="device-detail"><span>Remaining</span><strong id="device-remaining">0 s</strong></div>
                    <div class="device-detail"><span>Max Duration</span><strong>30 min</strong></div>
                </section>

                <section class="panel section-card">
                    <div class="section-heading compact">
                        <div>
                            <h2>Log</h2>
                        </div>
                        <button class="button button-secondary" type="button" data-action="clear-log">Clear</button>
                    </div>
                    <div class="log" id="log"></div>
                </section>
            </aside>
        </section>
    </main>

    <footer class="site-footer">ESP32 Smart Seedling Waterer</footer>
    <script src="app.js" defer></script>
</body>
</html>

)rawliteral";

const char STYLE_CSS[] PROGMEM = R"rawliteral(
* {
    box-sizing: border-box;
}

:root {
    font-family: Arial, Helvetica, sans-serif;
    color: #172033;
    background: #eef2f7;
}

body {
    margin: 0;
    min-height: 100vh;
}

button,
input,
select {
    font: inherit;
}

button {
    cursor: pointer;
}

.site-header {
    background: #111827;
    color: #fff;
    border-bottom: 4px solid #2563eb;
}

.header-content,
.app,
.site-footer {
    width: min(1500px, 96%);
    margin: 0 auto;
}

.header-content {
    padding: 18px 0;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 20px;
}

.site-title {
    margin: 0 0 6px;
    font-size: 25px;
}

.site-subtitle {
    margin: 0;
    color: #cbd5e1;
    font-size: 13px;
}

.connection-status {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 14px;
    font-weight: 700;
}

.connection-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #ef4444;
}

.connection-dot[data-state="online"] {
    background: #22c55e;
}

.app {
    margin-top: 18px;
    margin-bottom: 30px;
}

.panel {
    background: #fff;
    border: 1px solid #d7dde7;
    border-radius: 10px;
    box-shadow: 0 2px 10px rgba(15, 23, 42, 0.05);
}

.toolbar {
    padding: 12px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
}

.device-controls,
.program-controls,
.inline-field,
.section-heading,
.switch-row,
.device-detail {
    display: flex;
    align-items: center;
}

.device-controls,
.program-controls {
    gap: 7px;
    flex-wrap: wrap;
}

.section-heading {
    justify-content: space-between;
    gap: 12px;
}

.section-heading.compact {
    align-items: flex-start;
}

.field-label {
    font-size: 12px;
    font-weight: 700;
    color: #475569;
}

.text-input,
.select-input {
    width: 100%;
    padding: 8px 10px;
    border: 1px solid #cbd5e1;
    border-radius: 6px;
    background: #fff;
    color: #172033;
}

#device-address {
    width: 190px;
}

.button {
    border: 0;
    border-radius: 6px;
    padding: 8px 12px;
    color: #fff;
}

.button:hover {
    filter: brightness(0.94);
}

.button-primary {
    background: #2563eb;
}

.button-secondary {
    background: #64748b;
}

.button-success {
    background: #16a34a;
}

.button-danger {
    background: #dc2626;
}

.button-large {
    width: 100%;
    padding: 13px 12px;
    margin-top: 12px;
    font-weight: 800;
}

.status-grid {
    margin-top: 15px;
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 12px;
}

.status-card {
    padding: 16px;
}

.status-card-label,
.status-card-note {
    display: block;
}

.status-card-label {
    font-size: 12px;
    color: #64748b;
    font-weight: 700;
}

.status-card-value {
    display: block;
    margin: 8px 0 4px;
    font-size: 26px;
}

.status-card-note {
    font-size: 11px;
    color: #64748b;
}

.content-grid {
    margin-top: 15px;
    display: grid;
    grid-template-columns: minmax(0, 1fr) 330px;
    gap: 15px;
    align-items: start;
}

.main-column,
.side-column {
    display: flex;
    flex-direction: column;
    gap: 15px;
}

.section-card {
    padding: 15px;
}

.section-heading h2 {
    margin: 0;
    font-size: 18px;
}

.section-heading p {
    margin: 5px 0 0;
    color: #64748b;
    font-size: 12px;
}

.safety-note {
    display: block;
    margin-top: 8px;
    color: #64748b;
    font-size: 11px;
    line-height: 1.5;
}

.event-list {
    margin-top: 14px;
    display: flex;
    flex-direction: column;
    gap: 10px;
}

.event-card {
    padding: 12px;
    border: 1px solid #d7dde7;
    border-radius: 8px;
    background: #f8fafc;
}

.event-card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    margin-bottom: 10px;
}

.event-card-title {
    margin: 0;
    font-size: 13px;
}

.event-card-tools {
    display: flex;
    align-items: center;
    gap: 6px;
}

.event-delete {
    border: 0;
    background: #fee2e2;
    color: #b91c1c;
    border-radius: 5px;
    padding: 5px 8px;
}

.event-fields,
.condition-row,
.duration-row {
    display: grid;
    gap: 8px;
    align-items: end;
}

.schedule-fields {
    grid-template-columns: 150px minmax(170px, 1fr) 120px;
}

.condition-fields {
    grid-template-columns: 140px 90px 110px 140px 120px;
}

.second-condition-fields {
    grid-template-columns: 140px 90px 110px 90px 80px 140px;
}

.field-group {
    min-width: 0;
}

.field-group label {
    display: block;
    margin-bottom: 5px;
    color: #475569;
    font-size: 11px;
    font-weight: 700;
}

.condition-note {
    margin-top: 9px;
    color: #64748b;
    font-size: 11px;
}

.second-condition {
    margin-top: 10px;
    padding: 10px;
    border: 1px dashed #cbd5e1;
    border-radius: 7px;
    background: #fff;
}

.second-condition-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    margin-bottom: 8px;
}

.inline-check {
    display: flex;
    align-items: center;
    gap: 6px;
    color: #475569;
    font-size: 11px;
    font-weight: 700;
}

#manual-duration {
    flex: 1;
}

#manual-unit {
    width: 120px;
}

.safety-note {
    margin-bottom: 0;
}

.switch-row {
    gap: 9px;
    margin-top: 14px;
    font-size: 13px;
    font-weight: 700;
}

.switch-row input {
    width: 18px;
    height: 18px;
}

.device-detail {
    justify-content: space-between;
    gap: 10px;
    padding: 9px 0;
    border-bottom: 1px solid #eef2f7;
    font-size: 12px;
}

.device-detail:last-child {
    border-bottom: 0;
}

.log {
    height: 240px;
    overflow-y: auto;
    padding: 10px;
    margin-top: 10px;
    border-radius: 7px;
    background: #0f172a;
    color: #dbeafe;
    font-family: Consolas, Monaco, monospace;
    font-size: 11px;
    white-space: pre-wrap;
}

.log-line {
    margin-bottom: 5px;
}

.empty-message {
    margin-top: 14px;
    color: #94a3b8;
    font-size: 12px;
}

.site-footer {
    margin-bottom: 25px;
    text-align: center;
    color: #64748b;
    font-size: 12px;
}

@media (max-width: 1100px) {
    .content-grid {
        grid-template-columns: 1fr;
    }

    .side-column {
        display: grid;
        grid-template-columns: repeat(2, minmax(0, 1fr));
    }
}

@media (max-width: 820px) {
    .status-grid {
        grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .schedule-fields,
    .condition-fields,
    .second-condition-fields {
        grid-template-columns: 1fr;
    }

    #device-address {
        width: 100%;
    }

    .side-column {
        grid-template-columns: 1fr;
    }
}

@media (max-width: 540px) {
    .header-content,
    .toolbar,
    .section-heading {
        align-items: stretch;
    }

    .header-content,
    .section-heading {
        flex-direction: column;
    }

    .status-grid {
        grid-template-columns: 1fr;
    }
}

)rawliteral";

const char APP_JS[] PROGMEM = R"rawliteral(
const MAX_DURATION_SECONDS = 1800
const STORAGE_KEY = 'esp32-smart-seedling-controller-v2'

const state = {
    baseUrl: 'http://192.168.4.1',
    connected: false,
    automationEnabled: false,
    manualDurationSeconds: 30,
    schedules: [],
    conditions: [],
    pollTimer: null
}

const elements = {
    address: document.querySelector('#device-address'),
    connectionDot: document.querySelector('#connection-dot'),
    connectionLabel: document.querySelector('#connection-label'),
    time: document.querySelector('#status-time'),
    clockNote: document.querySelector('#status-clock'),
    temperature: document.querySelector('#status-temperature'),
    humidity: document.querySelector('#status-humidity'),
    water: document.querySelector('#status-water'),
    waterTime: document.querySelector('#status-water-time'),
    automation: document.querySelector('#device-automation'),
    reason: document.querySelector('#device-reason'),
    remaining: document.querySelector('#device-remaining'),
    manualDuration: document.querySelector('#manual-duration'),
    manualUnit: document.querySelector('#manual-unit'),
    schedules: document.querySelector('#schedule-list'),
    scheduleEmpty: document.querySelector('#schedule-empty'),
    conditions: document.querySelector('#condition-list'),
    conditionEmpty: document.querySelector('#condition-empty'),
    log: document.querySelector('#log')
}

function log(message) {
    const line = document.createElement('div')
    line.className = 'log-line'
    line.textContent = `${new Date().toLocaleTimeString()}  ${message}`
    elements.log.append(line)
    elements.log.scrollTop = elements.log.scrollHeight
}

function setConnection(connected) {
    state.connected = connected
    elements.connectionDot.dataset.state = connected ? 'online' : 'offline'
    elements.connectionLabel.textContent = connected ? 'Connected' : 'Offline'
}

function baseUrl() {
    return elements.address.value.trim().replace(/\/+$/, '')
}

async function request(path, options = {}) {
    state.baseUrl = baseUrl()
    return fetch(`${state.baseUrl}${path}`, {
        cache: 'no-store',
        ...options
    })
}

function toSeconds(amount, unit) {
    const value = Math.max(0, Number(amount) || 0)
    if (unit === 'minutes') {
        return Math.round(value * 60)
    }
    if (unit === 'hours') {
        return Math.round(value * 3600)
    }
    return Math.round(value)
}

function fromSeconds(seconds) {
    const safe = Math.max(0, Number(seconds) || 0)
    if (safe % 3600 === 0 && safe >= 3600) {
        return { amount: safe / 3600, unit: 'hours' }
    }
    if (safe % 60 === 0 && safe >= 60) {
        return { amount: safe / 60, unit: 'minutes' }
    }
    return { amount: safe, unit: 'seconds' }
}

function createField(label, input) {
    const wrap = document.createElement('div')
    wrap.className = 'field-group'
    const text = document.createElement('label')
    text.textContent = label
    wrap.append(text, input)
    return wrap
}

function input(type, value, min, max, step = 1) {
    const element = document.createElement('input')
    element.className = 'text-input'
    element.type = type
    element.value = value
    if (min !== undefined) element.min = min
    if (max !== undefined) element.max = max
    if (step !== undefined) element.step = step
    return element
}

function select(options, value) {
    const element = document.createElement('select')
    element.className = 'select-input'
    options.forEach(option => {
        const item = document.createElement('option')
        item.value = option.value
        item.textContent = option.label
        if (option.value === value) item.selected = true
        element.append(item)
    })
    return element
}

function conditionSensor(value) {
    return select([
        { value: 'temperature', label: 'Temperature' },
        { value: 'humidity', label: 'Humidity' }
    ], value)
}

function conditionOperator(value) {
    return select([
        { value: '<', label: '<' },
        { value: '<=', label: '<=' },
        { value: '>', label: '>' },
        { value: '>=', label: '>=' },
        { value: '==', label: '=' },
        { value: '!=', label: '!=' }
    ], value)
}

function durationEditor(title, seconds) {
    const wrap = document.createElement('div')
    wrap.className = 'duration-row'
    const value = fromSeconds(seconds)
    const amount = input('number', value.amount, 1, 1800)
    const unit = select([
        { value: 'seconds', label: 'seconds' },
        { value: 'minutes', label: 'minutes' },
        { value: 'hours', label: 'hours' }
    ], value.unit)
    wrap.append(createField(title, amount), createField('Unit', unit))
    return { wrap, amount, unit }
}

function scheduleCard(event, index) {
    const card = document.createElement('article')
    card.className = 'event-card'

    const header = document.createElement('div')
    header.className = 'event-card-header'
    const title = document.createElement('h3')
    title.className = 'event-card-title'
    title.textContent = `Daily Event ${index + 1}`
    const tools = document.createElement('div')
    tools.className = 'event-card-tools'
    const enabled = document.createElement('label')
    enabled.className = 'inline-check'
    const enabledBox = document.createElement('input')
    enabledBox.type = 'checkbox'
    enabledBox.checked = event.enabled !== false
    enabled.append(enabledBox, document.createTextNode('Enabled'))
    const remove = document.createElement('button')
    remove.className = 'event-delete'
    remove.type = 'button'
    remove.textContent = 'Delete'
    remove.addEventListener('click', () => {
        state.schedules.splice(index, 1)
        render()
        saveLocal()
    })
    tools.append(enabled, remove)
    header.append(title, tools)

    const grid = document.createElement('div')
    grid.className = 'event-fields schedule-fields'
    const time = input('time', event.time || '07:00')
    const duration = durationEditor('Water for', Math.min(MAX_DURATION_SECONDS, event.duration || 30))
    grid.append(
        createField('Time (24h)', time),
        duration.wrap
    )

    const note = document.createElement('div')
    note.className = 'condition-note'
    note.textContent = 'Runs once per day at this time.'

    time.addEventListener('change', () => saveLocal())
    enabledBox.addEventListener('change', () => saveLocal())
    duration.amount.addEventListener('change', () => saveLocal())
    duration.unit.addEventListener('change', () => saveLocal())

    card.append(header, grid, note)
    card.dataset.index = String(index)
    card.addEventListener('change', () => {
        event.enabled = enabledBox.checked
        event.time = time.value
        event.duration = Math.min(MAX_DURATION_SECONDS, toSeconds(duration.amount.value, duration.unit.value))
        saveLocal()
    })
    return card
}

function conditionCard(event, index) {
    const card = document.createElement('article')
    card.className = 'event-card'

    const header = document.createElement('div')
    header.className = 'event-card-header'
    const title = document.createElement('h3')
    title.className = 'event-card-title'
    title.textContent = `Environmental Rule ${index + 1}`
    const tools = document.createElement('div')
    tools.className = 'event-card-tools'
    const enabled = document.createElement('label')
    enabled.className = 'inline-check'
    const enabledBox = document.createElement('input')
    enabledBox.type = 'checkbox'
    enabledBox.checked = event.enabled !== false
    enabled.append(enabledBox, document.createTextNode('Enabled'))
    const remove = document.createElement('button')
    remove.className = 'event-delete'
    remove.type = 'button'
    remove.textContent = 'Delete'
    remove.addEventListener('click', () => {
        state.conditions.splice(index, 1)
        render()
        saveLocal()
    })
    tools.append(enabled, remove)
    header.append(title, tools)

    const row1 = document.createElement('div')
    row1.className = 'event-fields condition-fields'
    const sensor1 = conditionSensor(event.sensor1 || 'temperature')
    const operator1 = conditionOperator(event.operator1 || '>')
    const threshold1 = input('number', event.threshold1 ?? 35, -50, 100, 0.1)
    const persistence = durationEditor('Condition stays true for', event.persistenceSeconds || 60)
    const watering = durationEditor('Water for', Math.min(MAX_DURATION_SECONDS, event.duration || 30))
    row1.append(
        createField('Sensor', sensor1),
        createField('Operator', operator1),
        createField('Threshold', threshold1),
        persistence.wrap,
        watering.wrap
    )

    const row2 = document.createElement('div')
    row2.className = 'event-fields second-condition-fields second-condition'
    const secondHead = document.createElement('div')
    secondHead.className = 'second-condition-head'
    const secondToggle = document.createElement('label')
    secondToggle.className = 'inline-check'
    const secondEnabled = document.createElement('input')
    secondEnabled.type = 'checkbox'
    secondEnabled.checked = Boolean(event.secondEnabled)
    secondToggle.append(secondEnabled, document.createTextNode('Use second condition'))
    const logic = select([
        { value: 'AND', label: 'AND' },
        { value: 'OR', label: 'OR' }
    ], event.logic || 'AND')
    secondHead.append(secondToggle, createField('Join', logic))

    const sensor2 = conditionSensor(event.sensor2 || 'humidity')
    const operator2 = conditionOperator(event.operator2 || '<')
    const threshold2 = input('number', event.threshold2 ?? 40, -50, 100, 0.1)
    row2.append(
        secondHead,
        createField('Sensor 2', sensor2),
        createField('Operator 2', operator2),
        createField('Threshold 2', threshold2)
    )

    const cooldown = durationEditor('Cooldown after watering', event.cooldownSeconds || 3600)
    const cooldownBox = document.createElement('div')
    cooldownBox.append(cooldown.wrap)

    const note = document.createElement('div')
    note.className = 'condition-note'
    note.textContent = 'The combined condition must remain true continuously before watering is triggered.'

    card.append(header, row1, row2, cooldownBox, note)

    function updateVisibility() {
        sensor2.disabled = !secondEnabled.checked
        operator2.disabled = !secondEnabled.checked
        threshold2.disabled = !secondEnabled.checked
        logic.disabled = !secondEnabled.checked
    }

    function capture() {
        event.enabled = enabledBox.checked
        event.sensor1 = sensor1.value
        event.operator1 = operator1.value
        event.threshold1 = Number(threshold1.value) || 0
        event.persistenceSeconds = toSeconds(persistence.amount.value, persistence.unit.value)
        event.duration = Math.min(MAX_DURATION_SECONDS, toSeconds(watering.amount.value, watering.unit.value))
        event.secondEnabled = secondEnabled.checked
        event.logic = logic.value
        event.sensor2 = sensor2.value
        event.operator2 = operator2.value
        event.threshold2 = Number(threshold2.value) || 0
        event.cooldownSeconds = toSeconds(cooldown.amount.value, cooldown.unit.value)
        updateVisibility()
        saveLocal()
    }

    card.querySelectorAll('input, select').forEach(element => element.addEventListener('change', capture))
    updateVisibility()
    return card
}

function render() {
    elements.schedules.replaceChildren()
    elements.conditions.replaceChildren()

    state.schedules.forEach((event, index) => {
        elements.schedules.append(scheduleCard(event, index))
    })
    state.conditions.forEach((event, index) => {
        elements.conditions.append(conditionCard(event, index))
    })

    elements.scheduleEmpty.hidden = state.schedules.length > 0
    elements.conditionEmpty.hidden = state.conditions.length > 0
    elements.manualDuration.value = state.manualDurationSeconds >= 60 && state.manualDurationSeconds % 60 === 0
        ? state.manualDurationSeconds / 60
        : state.manualDurationSeconds
    elements.manualUnit.value = state.manualDurationSeconds >= 60 && state.manualDurationSeconds % 60 === 0
        ? 'minutes'
        : 'seconds'
}

function stateFromStatus(status) {
    if (status.automation !== undefined) state.automationEnabled = Boolean(status.automation)
}

function updateStatus(status) {
    stateFromStatus(status)
    elements.temperature.textContent = status.temperature === null ? '--' : `${Number(status.temperature).toFixed(1)} \u00B0C`
    elements.humidity.textContent = status.humidity === null ? '--' : `${Number(status.humidity).toFixed(1)} %`
    elements.water.textContent = status.water ? 'ON' : 'OFF'
    elements.waterTime.textContent = status.water ? `${Number(status.remaining).toFixed(0)} s remaining` : 'Ready'
    elements.automation.textContent = state.automationEnabled ? 'ON' : 'OFF'
    elements.reason.textContent = status.reason || 'None'
    elements.remaining.textContent = `${Number(status.remaining || 0).toFixed(0)} s`
    const timeText = status.time || '--:--:--'
    elements.time.textContent = timeText
    elements.clockNote.textContent = status.timeValid ? (status.rtc ? 'DS3231 RTC active' : 'Clock active') : 'Clock not synced'
}

function saveLocal() {
    const payload = {
        automationEnabled: state.automationEnabled,
        manualDurationSeconds: state.manualDurationSeconds,
        schedules: state.schedules,
        conditions: state.conditions
    }
    localStorage.setItem(STORAGE_KEY, JSON.stringify(payload))
}

function loadLocal() {
    try {
        const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || 'null')
        if (!saved) return
        state.automationEnabled = Boolean(saved.automationEnabled)
            state.manualDurationSeconds = Math.min(MAX_DURATION_SECONDS, Number(saved.manualDurationSeconds) || 30)
        state.schedules = Array.isArray(saved.schedules) ? saved.schedules : []
        state.conditions = Array.isArray(saved.conditions) ? saved.conditions : []
    } catch {
        return
    }
}

async function syncTime() {
    if (!state.connected) return
    const now = new Date()
    const localEpoch = Math.floor(Date.UTC(
        now.getFullYear(),
        now.getMonth(),
        now.getDate(),
        now.getHours(),
        now.getMinutes(),
        now.getSeconds()
    ) / 1000)
    try {
        const response = await request(`/api/time?unix=${localEpoch}`, { method: 'POST' })
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        log('Device clock synchronized automatically')
        refreshStatus()
    } catch (error) {
        log(`Automatic clock sync failed: ${error.message}`)
    }
}

function serializeConfig() {
    const lines = [
        `A|${state.automationEnabled ? 1 : 0}`,
        `M|${Math.min(MAX_DURATION_SECONDS, state.manualDurationSeconds)}`
    ]

    state.schedules.slice(0, 10).forEach(event => {
        lines.push(`E|S|${event.enabled === false ? 0 : 1}|${event.time || '07:00'}|${Math.min(MAX_DURATION_SECONDS, Math.max(1, Number(event.duration) || 1))}`)
    })

    state.conditions.slice(0, 10).forEach(event => {
        lines.push([
            'E', 'C',
            event.enabled === false ? 0 : 1,
            event.sensor1 || 'temperature',
            event.operator1 || '>',
            Number(event.threshold1) || 0,
            Math.max(1, Number(event.persistenceSeconds) || 1),
            Math.min(MAX_DURATION_SECONDS, Math.max(1, Number(event.duration) || 1)),
            Math.max(0, Number(event.cooldownSeconds) || 0),
            event.secondEnabled ? 1 : 0,
            event.logic || 'AND',
            event.sensor2 || 'humidity',
            event.operator2 || '<',
            Number(event.threshold2) || 0
        ].join('|'))
    })

    return `${lines.join('\n')}\n`
}

async function saveToESP32() {
    if (!state.connected) {
        log('Connect to the ESP32 first')
        return
    }
    captureManualDuration()
    state.automationEnabled = state.automationEnabled
    saveLocal()
    try {
        const response = await request('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: serializeConfig()
        })
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        log('Configuration saved to ESP32')
        refreshStatus()
    } catch (error) {
        log(`Save failed: ${error.message}`)
    }
}

async function loadFromESP32() {
    if (!state.connected) return
    try {
        const response = await request('/api/config', { cache: 'no-store' })
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        const text = await response.text()
        const serverConfig = parseConfig(text, true)
        if (serverConfig.hasSavedConfig) {
            parseConfig(text, false)
            render()
            saveLocal()
            log(`Configuration loaded from ESP32: ${serverConfig.schedules} scheduled, ${serverConfig.conditions} environmental`)
        } else {
            render()
            saveLocal()
            log('ESP32 has no saved watering rules yet')
        }
    } catch (error) {
        log(`Load failed: ${error.message}`)
    }
}

function parseConfig(text, inspectOnly = false) {
    const schedules = []
    const conditions = []
    let automation = false
    let manual = 30

    text.split(/\r?\n/).forEach(line => {
        const parts = line.trim().split('|')
        if (!parts[0]) return
        if (parts[0] === 'A') automation = parts[1] === '1'
        if (parts[0] === 'M') manual = Math.min(MAX_DURATION_SECONDS, Number(parts[1]) || 30)
        if (parts[0] !== 'E') return

        if (parts[1] === 'S') {
            schedules.push({
                enabled: parts[2] !== '0',
                time: parts[3] || '07:00',
                duration: Math.min(MAX_DURATION_SECONDS, Number(parts[4]) || 30)
            })
        }

        if (parts[1] === 'C') {
            conditions.push({
                enabled: parts[2] !== '0',
                sensor1: parts[3] || 'temperature',
                operator1: parts[4] || '>',
                threshold1: Number(parts[5]) || 0,
                persistenceSeconds: Number(parts[6]) || 60,
                duration: Math.min(MAX_DURATION_SECONDS, Number(parts[7]) || 30),
                cooldownSeconds: Number(parts[8]) || 3600,
                secondEnabled: parts[9] === '1',
                logic: parts[10] || 'AND',
                sensor2: parts[11] || 'humidity',
                operator2: parts[12] || '<',
                threshold2: Number(parts[13]) || 0
            })
        }
    })

    const hasSavedConfig = schedules.length > 0 || conditions.length > 0 || automation || manual !== 30

    if (!inspectOnly || hasSavedConfig) {
        state.automationEnabled = automation
        state.manualDurationSeconds = manual
        state.schedules = schedules
        state.conditions = conditions
    }

    return { hasSavedConfig, schedules: schedules.length, conditions: conditions.length }
}

async function connect() {
    state.baseUrl = baseUrl()
    try {
        const response = await request('/api/status')
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        const status = await response.json()
        setConnection(true)
        updateStatus(status)
        log('ESP32 connection successful')
        await syncTime()
        await loadFromESP32()
        startPolling()
    } catch (error) {
        setConnection(false)
        log(`Connection failed: ${error.message}`)
    }
}

function disconnect() {
    stopPolling()
    setConnection(false)
    log('Disconnected')
}

function startPolling() {
    stopPolling()
    state.pollTimer = setInterval(refreshStatus, 1000)
}

function stopPolling() {
    if (state.pollTimer) clearInterval(state.pollTimer)
    state.pollTimer = null
}

async function refreshStatus() {
    if (!state.connected) return
    try {
        const response = await request('/api/status')
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        updateStatus(await response.json())
    } catch {
        setConnection(false)
        stopPolling()
        log('ESP32 connection lost')
    }
}

function captureManualDuration() {
    state.manualDurationSeconds = Math.min(
        MAX_DURATION_SECONDS,
        Math.max(1, toSeconds(elements.manualDuration.value, elements.manualUnit.value))
    )
}

async function manualWater() {
    if (!state.connected) {
        log('Connect to the ESP32 first')
        return
    }
    captureManualDuration()
    elements.manualDuration.value = fromSeconds(state.manualDurationSeconds).amount
    elements.manualUnit.value = fromSeconds(state.manualDurationSeconds).unit === 'hours' ? 'minutes' : fromSeconds(state.manualDurationSeconds).unit
    saveLocal()
    try {
        const response = await request(`/api/manual?duration=${state.manualDurationSeconds}`, { method: 'POST' })
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        log(`Manual watering started for ${state.manualDurationSeconds} seconds`)
        refreshStatus()
    } catch (error) {
        log(`Manual watering failed: ${error.message}`)
    }
}

async function forceStopWatering() {
    if (!state.connected) {
        log('Connect to the ESP32 first')
        return
    }
    try {
        const response = await request('/api/force-stop', { method: 'POST' })
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        log('Watering force stopped')
        refreshStatus()
    } catch (error) {
        log(`Force stop failed: ${error.message}`)
    }
}

async function setAutomation(enabled) {
    state.automationEnabled = enabled
    saveLocal()
    if (!state.connected) return
    try {
        const response = await request(enabled ? '/api/run' : '/api/stop', { method: 'POST' })
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        log(enabled ? 'Automation enabled' : 'Automation stopped')
        refreshStatus()
    } catch (error) {
        log(`Automation change failed: ${error.message}`)
    }
}

function addSchedule() {
    if (state.schedules.length >= 10) return
    state.schedules.push({ enabled: true, time: '07:00', duration: 60 })
    render()
    saveLocal()
}

function addCondition() {
    if (state.conditions.length >= 10) return
    state.conditions.push({
        enabled: true,
        sensor1: 'temperature',
        operator1: '>',
        threshold1: 35,
        persistenceSeconds: 600,
        duration: 60,
        cooldownSeconds: 3600,
        secondEnabled: false,
        logic: 'AND',
        sensor2: 'humidity',
        operator2: '<',
        threshold2: 40
    })
    render()
    saveLocal()
}

function clearLog() {
    elements.log.replaceChildren()
}

document.addEventListener('click', event => {
    const action = event.target.closest('[data-action]')
    if (!action) return
    const name = action.dataset.action
    if (name === 'connect') connect()
    if (name === 'disconnect') disconnect()
    if (name === 'save') saveToESP32()
    if (name === 'run') setAutomation(true)
    if (name === 'stop') setAutomation(false)
    if (name === 'manual-water') manualWater()
    if (name === 'force-stop') forceStopWatering()
    if (name === 'add-schedule') addSchedule()
    if (name === 'add-condition') addCondition()
    if (name === 'clear-log') clearLog()
})

elements.manualDuration.addEventListener('change', () => {
    captureManualDuration()
    saveLocal()
})

elements.manualUnit.addEventListener('change', () => {
    captureManualDuration()
    saveLocal()
})

loadLocal()
render()
log('Ready')

)rawliteral";

void addCors() {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type", true);
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS", true);
}

void sendText(int code, const String& body, const char* type = "text/plain") {
    addCors();
    server.send(code, type, body);
}

void sendJson(int code, const String& body) {
    sendText(code, body, "application/json");
}

String jsonEscape(const String& value) {
    String result;
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '"' || c == '\\') result += '\\';
        result += c;
    }
    return result;
}

bool timeValid() {
    if (!rtcAvailable) return false;
    DateTime now = rtc.now();
    return now.year() >= 2024;
}

int64_t rtcUnixTime() {
    if (!timeValid()) return -1;
    return (int64_t)rtc.now().unixtime();
}

int64_t dayKey() {
    int64_t unixNow = rtcUnixTime();
    if (unixNow < 0) return -1;
    return unixNow / 86400LL;
}

int secondsOfDay() {
    if (!timeValid()) return -1;
    DateTime now = rtc.now();
    return now.hour() * 3600 + now.minute() * 60 + now.second();
}

String formatTime() {
    if (!timeValid()) return "--:--:--";
    DateTime now = rtc.now();
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    return String(buffer);
}

bool initRtc() {
    Wire.begin(RTC_SDA, RTC_SCL);
    Wire.setClock(100000);
    delay(10);

    bool busSeesRtc = false;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        Wire.beginTransmission(0x68);
        uint8_t error = Wire.endTransmission();
        if (error == 0) {
            busSeesRtc = true;
            break;
        }
        delay(20);
    }

    Serial.print("I2C 0x68: ");
    Serial.println(busSeesRtc ? "DETECTED" : "NOT DETECTED");

    if (!busSeesRtc) return false;

    if (!rtc.begin(&Wire)) {
        Serial.println("RTClib could not initialize DS3231.");
        return false;
    }

    rtc.disable32K();
    rtc.writeSqwPinMode(DS3231_OFF);
    return true;
}

bool syncSystemClockFromRtc() {
    if (!rtcAvailable || !timeValid()) return false;
    DateTime now = rtc.now();
    struct timeval tv;
    tv.tv_sec = (time_t)now.unixtime();
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    return true;
}

void setRtcFromUnix(int64_t unixTime) {
    if (!rtcAvailable || unixTime <= 0) return;
    DateTime value((uint32_t)unixTime);
    rtc.adjust(value);
    syncSystemClockFromRtc();
}

void clearRtcEventState() {
    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        rtcLastScheduleDay[i] = -1;
        rtcConditionSince[i] = -1;
        rtcLastConditionTrigger[i] = -1;
    }
}

void loadConfig() {
    memset(&config, 0, sizeof(config));
    bool valid = false;
    size_t size = prefs.getBytesLength("cfg");
    if (size == sizeof(config)) {
        valid = prefs.getBytes("cfg", &config, sizeof(config)) == sizeof(config);
    }
    if (!valid || config.magic != CONFIG_MAGIC || config.eventCount > MAX_EVENTS) {
        config.magic = CONFIG_MAGIC;
        config.automationEnabled = false;
        config.manualDurationSec = 30;
        config.eventCount = 0;
    }
    if (config.manualDurationSec == 0 || config.manualDurationSec > MAX_DURATION_SEC) config.manualDurationSec = 30;
    for (uint8_t i = 0; i < config.eventCount; i++) {
        if (config.events[i].durationSec == 0 || config.events[i].durationSec > MAX_DURATION_SEC) config.events[i].durationSec = 30;
        if (config.events[i].type > 1) config.events[i].type = 0;
    }
}

void saveConfig() {
    config.magic = CONFIG_MAGIC;
    config.manualDurationSec = constrain(config.manualDurationSec, 1UL, MAX_DURATION_SEC);
    if (config.eventCount > MAX_EVENTS) config.eventCount = MAX_EVENTS;
    prefs.putBytes("cfg", &config, sizeof(config));
}

uint8_t sensorCode(const String& value) {
    return value == "humidity" ? 1 : 0;
}

String sensorName(uint8_t value) {
    return value == 1 ? "humidity" : "temperature";
}

uint8_t opCode(const String& value) {
    if (value == "<") return 0;
    if (value == "<=") return 1;
    if (value == ">") return 2;
    if (value == ">=") return 3;
    if (value == "==") return 4;
    return 5;
}

String opName(uint8_t value) {
    if (value == 0) return "<";
    if (value == 1) return "<=";
    if (value == 2) return ">";
    if (value == 3) return ">=";
    if (value == 4) return "==";
    return "!=";
}

bool parseBoolField(const String& value) {
    return value == "1" || value == "true";
}

int splitFields(const String& line, String fields[], int maxFields) {
    int count = 0;
    int start = 0;
    while (count < maxFields) {
        int separator = line.indexOf('|', start);
        if (separator < 0) {
            fields[count++] = line.substring(start);
            break;
        }
        fields[count++] = line.substring(start, separator);
        start = separator + 1;
    }
    return count;
}

bool applyConfigText(const String& text) {
    DeviceConfig next = config;
    next.eventCount = 0;
    next.automationEnabled = false;
    next.manualDurationSec = 30;
    memset(next.events, 0, sizeof(next.events));

    int start = 0;
    while (start < (int)text.length()) {
        int end = text.indexOf('\n', start);
        if (end < 0) end = text.length();
        String line = text.substring(start, end);
        line.trim();
        start = end + 1;
        if (!line.length()) continue;

        String fields[16];
        int count = splitFields(line, fields, 16);
        if (count == 0) continue;

        if (fields[0] == "A" && count >= 2) {
            next.automationEnabled = parseBoolField(fields[1]);
            continue;
        }
        if (fields[0] == "M" && count >= 2) {
            next.manualDurationSec = constrain((uint32_t)fields[1].toInt(), 1UL, MAX_DURATION_SEC);
            continue;
        }
        if (fields[0] != "E" || count < 2 || next.eventCount >= MAX_EVENTS) continue;

        EventConfig& event = next.events[next.eventCount];
        event.enabled = true;
        event.durationSec = 30;
        event.persistenceSec = 60;
        event.cooldownSec = 3600;
        event.secondEnabled = false;
        event.logic = 0;

        if (fields[1] == "S" && count >= 5) {
            event.type = 0;
            event.enabled = parseBoolField(fields[2]);
            snprintf(event.time, sizeof(event.time), "%s", fields[3].c_str());
            event.durationSec = constrain((uint32_t)fields[4].toInt(), 1UL, MAX_DURATION_SEC);
            next.eventCount++;
            continue;
        }

        if (fields[1] == "C" && count >= 14) {
            event.type = 1;
            event.enabled = parseBoolField(fields[2]);
            event.first.enabled = true;
            event.first.sensor = sensorCode(fields[3]);
            event.first.op = opCode(fields[4]);
            event.first.threshold = fields[5].toFloat();
            event.persistenceSec = max(1L, fields[6].toInt());
            event.durationSec = constrain((uint32_t)fields[7].toInt(), 1UL, MAX_DURATION_SEC);
            event.cooldownSec = max(0L, fields[8].toInt());
            event.secondEnabled = parseBoolField(fields[9]);
            event.logic = fields[10] == "OR" ? 1 : 0;
            event.second.enabled = event.secondEnabled;
            event.second.sensor = sensorCode(fields[11]);
            event.second.op = opCode(fields[12]);
            event.second.threshold = fields[13].toFloat();
            next.eventCount++;
        }
    }

    next.magic = CONFIG_MAGIC;
    config = next;
    saveConfig();
    clearRtcEventState();
    return true;
}

String configText() {
    String text;
    text.reserve(2500);
    text += "A|";
    text += config.automationEnabled ? "1\n" : "0\n";
    text += "M|";
    text += String(config.manualDurationSec);
    text += "\n";

    for (uint8_t i = 0; i < config.eventCount; i++) {
        const EventConfig& event = config.events[i];
        if (event.type == 0) {
            text += "E|S|";
            text += event.enabled ? "1|" : "0|";
            text += String(event.time);
            text += "|";
            text += String(event.durationSec);
            text += "\n";
        } else {
            text += "E|C|";
            text += event.enabled ? "1|" : "0|";
            text += sensorName(event.first.sensor);
            text += "|";
            text += opName(event.first.op);
            text += "|";
            text += String(event.first.threshold, 2);
            text += "|";
            text += String(event.persistenceSec);
            text += "|";
            text += String(event.durationSec);
            text += "|";
            text += String(event.cooldownSec);
            text += "|";
            text += event.secondEnabled ? "1|" : "0|";
            text += event.logic ? "OR|" : "AND|";
            text += sensorName(event.second.sensor);
            text += "|";
            text += opName(event.second.op);
            text += "|";
            text += String(event.second.threshold, 2);
            text += "\n";
        }
    }
    return text;
}

bool compareValue(float actual, uint8_t op, float target) {
    if (isnan(actual)) return false;
    if (op == 0) return actual < target;
    if (op == 1) return actual <= target;
    if (op == 2) return actual > target;
    if (op == 3) return actual >= target;
    if (op == 4) return fabs(actual - target) < 0.05f;
    return fabs(actual - target) >= 0.05f;
}

float readSensor(uint8_t sensor) {
    return sensor == 1 ? currentHumidity : currentTemperature;
}

bool eventCondition(const EventConfig& event) {
    bool first = compareValue(readSensor(event.first.sensor), event.first.op, event.first.threshold);
    if (!event.secondEnabled) return first;
    bool second = compareValue(readSensor(event.second.sensor), event.second.op, event.second.threshold);
    return event.logic == 1 ? (first || second) : (first && second);
}

void readSensors() {
    if (millis() - lastSensorReadMs < SENSOR_INTERVAL_MS && !isnan(currentTemperature)) return;
    lastSensorReadMs = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h)) currentHumidity = h;
    if (!isnan(t)) currentTemperature = t;
}

void stopWatering(const String& reason) {
    digitalWrite(WATER_PIN, LOW);
    waterState = false;
    waterStartMs = 0;
    waterDurationMs = 0;
    lastReason = reason;
}

void startWatering(uint32_t durationSec, const String& reason) {
    uint32_t safeDuration = constrain(durationSec, 1UL, MAX_DURATION_SEC);
    digitalWrite(WATER_PIN, HIGH);
    waterState = true;
    waterStartMs = millis();
    waterDurationMs = safeDuration * 1000UL;
    lastReason = reason;
}

void updateWatering() {
    if (!waterState) return;
    if ((uint32_t)(millis() - waterStartMs) >= waterDurationMs) stopWatering("Watering complete");
}

void markScheduleStateAfterClockSync() {
    if (!timeValid()) return;
    int64_t day = dayKey();
    int nowSec = secondsOfDay();
    for (uint8_t i = 0; i < config.eventCount; i++) {
        if (config.events[i].type != 0) continue;
        int h = atoi(String(config.events[i].time).substring(0, 2).c_str());
        int m = atoi(String(config.events[i].time).substring(3, 5).c_str());
        int target = h * 3600 + m * 60;
        if (target <= nowSec) rtcLastScheduleDay[i] = day;
    }
}

bool scheduledDue(uint8_t index) {
    if (!timeValid()) return false;
    EventConfig& event = config.events[index];
    if (!event.enabled || event.type != 0) return false;
    if (rtcLastScheduleDay[index] == dayKey()) return false;
    String timeText = String(event.time);
    if (timeText.length() < 5) return false;
    int hour = timeText.substring(0, 2).toInt();
    int minute = timeText.substring(3, 5).toInt();
    int target = hour * 3600 + minute * 60;
    int nowSec = secondsOfDay();
    if (nowSec < target) return false;
    if (nowSec - target > 120) return false;
    return true;
}

void processScheduledEvents() {
    if (!config.automationEnabled || waterState) return;
    int64_t today = dayKey();
    for (uint8_t i = 0; i < config.eventCount; i++) {
        if (!scheduledDue(i)) continue;
        rtcLastScheduleDay[i] = today;
        startWatering(config.events[i].durationSec, "Scheduled watering");
        return;
    }
}

void processConditionEvents() {
    if (!config.automationEnabled || waterState) return;
    for (uint8_t i = 0; i < config.eventCount; i++) {
        EventConfig& event = config.events[i];
        if (!event.enabled || event.type != 1) {
            rtcConditionSince[i] = -1;
            continue;
        }

        bool trueNow = eventCondition(event);
        if (!trueNow) {
            rtcConditionSince[i] = -1;
            continue;
        }

        uint32_t nowMs = millis();
        if (rtcConditionSince[i] < 0) rtcConditionSince[i] = (int64_t)nowMs;

        uint32_t elapsedMs = (uint32_t)(nowMs - (uint32_t)rtcConditionSince[i]);
        if ((uint64_t)elapsedMs < (uint64_t)event.persistenceSec * 1000ULL) continue;

        if (rtcLastConditionTrigger[i] >= 0) {
            uint32_t sinceTriggerMs = (uint32_t)(nowMs - (uint32_t)rtcLastConditionTrigger[i]);
            if ((uint64_t)sinceTriggerMs < (uint64_t)event.cooldownSec * 1000ULL) continue;
        }

        rtcLastConditionTrigger[i] = (int64_t)nowMs;
        startWatering(event.durationSec, "Environmental rule");
        return;
    }
}

void setupWifi() {
    WiFi.mode(WIFI_AP);
    delay(500);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(WIFI_SSID, nullptr, 1, false, 4);
}

void sendStatus() {
    String json = "{";
    json += "\"temperature\":";
    json += isnan(currentTemperature) ? "null" : String(currentTemperature, 1);
    json += ",\"humidity\":";
    json += isnan(currentHumidity) ? "null" : String(currentHumidity, 1);
    json += ",\"water\":";
    json += waterState ? "true" : "false";
    json += ",\"remaining\":";
    uint32_t remaining = 0;
    if (waterState) {
        uint32_t elapsedMs = (uint32_t)(millis() - waterStartMs);
        if (elapsedMs < waterDurationMs) remaining = (waterDurationMs - elapsedMs + 999UL) / 1000UL;
    }
    json += String(remaining);
    json += ",\"automation\":";
    json += config.automationEnabled ? "true" : "false";
    json += ",\"timeValid\":";
    json += timeValid() ? "true" : "false";
    json += ",\"rtc\":";
    json += rtcAvailable ? "true" : "false";
    json += ",\"time\":\"";
    json += formatTime();
    json += "\",\"reason\":\"";
    json += jsonEscape(lastReason);
    json += "\",\"ssid\":\"";
    json += WIFI_SSID;
    json += "\",\"ip\":\"";
    json += WiFi.softAPIP().toString();
    json += "\",\"maxDuration\":";
    json += String(MAX_DURATION_SEC);
    json += "}";
    sendJson(200, json);
}

void setupRoutes() {
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    server.on("/style.css", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.send_P(200, "text/css; charset=utf-8", STYLE_CSS);
    });

    server.on("/app.js", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.send_P(200, "application/javascript; charset=utf-8", APP_JS);
    });

    server.on("/api/status", HTTP_GET, []() {
        sendStatus();
    });

    server.on("/api/config", HTTP_GET, []() {
        addCors();
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.send(200, "text/plain", configText());
    });

    server.on("/api/config", HTTP_POST, []() {
        if (!applyConfigText(server.arg("plain"))) {
            sendJson(400, "{\"error\":\"Invalid configuration\"}");
            return;
        }
        sendJson(200, "{\"ok\":true}");
    });

    server.on("/api/time", HTTP_POST, []() {
        if (!server.hasArg("unix")) {
            sendJson(400, "{\"error\":\"Missing unix time\"}");
            return;
        }
        bool wasValid = timeValid();
        int64_t value = strtoll(server.arg("unix").c_str(), nullptr, 10);
        if (!rtcAvailable) {
            sendJson(503, "{\"error\":\"RTC unavailable\"}");
            return;
        }
        setRtcFromUnix(value);
        if (!wasValid) clearRtcEventState();
        markScheduleStateAfterClockSync();
        sendJson(200, "{\"ok\":true}");
    });

    server.on("/api/manual", HTTP_POST, []() {
        uint32_t duration = config.manualDurationSec;
        if (server.hasArg("duration")) duration = (uint32_t)server.arg("duration").toInt();
        duration = constrain(duration, 1UL, MAX_DURATION_SEC);
        config.manualDurationSec = duration;
        saveConfig();
        startWatering(duration, "Manual watering");
        sendJson(200, "{\"ok\":true}");
    });

    server.on("/api/force-stop", HTTP_POST, []() {
        stopWatering("Force stopped");
        sendJson(200, "{\"ok\":true}");
    });

    server.on("/api/run", HTTP_POST, []() {
        config.automationEnabled = true;
        saveConfig();
        sendJson(200, "{\"ok\":true}");
    });

    server.on("/api/stop", HTTP_POST, []() {
        config.automationEnabled = false;
        saveConfig();
        stopWatering("Automation stopped");
        sendJson(200, "{\"ok\":true}");
    });

    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) {
            addCors();
            server.send(204);
            return;
        }
        sendText(404, "Not found");
    });
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(WATER_PIN, OUTPUT);
    digitalWrite(WATER_PIN, LOW);

    dht.begin();
    prefs.begin("seedling", false);
    loadConfig();

    rtcAvailable = initRtc();
    if (rtcAvailable) {
        if (rtc.lostPower()) {
            Serial.println("RTC lost power. Waiting for time synchronization.");
        }
        syncSystemClockFromRtc();
    } else {
        Serial.println("DS3231 RTC not detected.");
    }

    if (!rtcStateInitialized) {
        clearRtcEventState();
        rtcStateInitialized = true;
    }

    setupWifi();
    setupRoutes();
    server.begin();

    readSensors();

    Serial.println();
    Serial.println("========================================");
    Serial.println("ESP32 SMART SEEDLING WATERER");
    Serial.println("========================================");
    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("24-hour schedule enabled");
    Serial.println("Maximum watering duration: 30 minutes");
    Serial.print("DS3231: ");
    Serial.println(rtcAvailable ? (timeValid() ? "OK" : "CONNECTED, TIME NOT SET") : "NOT DETECTED");
    Serial.println("I2C: SDA GPIO6, SCL GPIO7");
}

void loop() {
    server.handleClient();
    readSensors();
    updateWatering();

    if (config.automationEnabled && !waterState) {
        processScheduledEvents();
        if (!waterState) processConditionEvents();
    }

    delay(1);
}
