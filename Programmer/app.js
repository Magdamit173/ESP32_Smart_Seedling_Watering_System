const PROGRAM_STORAGE_KEY =
    'esp32-smart-seedling-program'

const VARIABLES_STORAGE_KEY =
    'esp32-smart-seedling-variables'


const state = {
    baseUrl: 'http://192.168.4.1',
    connected: false,

    program: {
        name: 'My Program',
        steps: []
    },

    variables: [],

    nextBlockId: 1,

    dragging: {
        paletteType: null,
        block: null
    },

    statusTimer: null
}


const elements = {
    deviceAddress:
        document.querySelector(
            '#device-address'
        ),

    programName:
        document.querySelector(
            '[data-field="program-name"]'
        ),

    workspace:
        document.querySelector(
            '[data-workspace="program"]'
        ),

    connectionDot:
        document.querySelector(
            '#connection-dot'
        ),

    connectionLabel:
        document.querySelector(
            '#connection-label'
        ),

    serialOutput:
        document.querySelector(
            '[data-output="serial"]'
        ),

    variableList:
        document.querySelector(
            '#variable-list'
        ),

    newVariableName:
        document.querySelector(
            '[data-field="new-variable-name"]'
        ),

    temperature:
        document.querySelector(
            '#status-temperature'
        ),

    humidity:
        document.querySelector(
            '#status-humidity'
        ),

    water:
        document.querySelector(
            '#status-water'
        ),

    running:
        document.querySelector(
            '#status-running'
        ),

    displayGrid:
        document.querySelector(
            '#user-display-grid'
        )
}


function createElement(
    tag,
    className = '',
    text
) {
    const element =
        document.createElement(tag)

    if (className) {
        element.className =
            className
    }

    if (text !== undefined) {
        element.textContent =
            text
    }

    return element
}


function delay(milliseconds) {
    return new Promise(
        resolve =>
            setTimeout(
                resolve,
                milliseconds
            )
    )
}


function appendLog(message) {
    const line =
        createElement(
            'div',
            'monitor-line',
            message
        )

    elements.serialOutput.append(
        line
    )

    elements.serialOutput.scrollTop =
        elements.serialOutput.scrollHeight
}


function clearLog() {
    elements.serialOutput.replaceChildren()
}


function setConnectionState(
    connected
) {
    state.connected =
        connected

    elements.connectionDot.dataset.state =
        connected
            ? 'online'
            : 'offline'

    elements.connectionLabel.dataset.state =
        connected
            ? 'online'
            : 'offline'

    elements.connectionLabel.textContent =
        connected
            ? 'Connected'
            : 'Offline'
}


function normalizeBaseUrl(value) {
    return value
        .trim()
        .replace(
            /\/+$/,
            ''
        )
}


async function fetchESP32(
    path,
    options = {}
) {
    state.baseUrl =
        normalizeBaseUrl(
            elements.deviceAddress.value
        )

    const response =
        await fetch(
            `${state.baseUrl}${path}`,
            {
                ...options,
                cache: 'no-store'
            }
        )

    if (!response.ok) {
        throw new Error(
            `HTTP ${response.status}`
        )
    }

    return response
}


async function connectESP32() {
    state.baseUrl =
        normalizeBaseUrl(
            elements.deviceAddress.value
        )

    if (!state.baseUrl) {
        appendLog(
            'ESP32 address is empty'
        )

        return
    }

    appendLog(
        `Connecting to ${state.baseUrl}`
    )

    try {
        const response =
            await fetchESP32(
                '/api/status'
            )

        const status =
            await response.json()

        setConnectionState(
            true
        )

        appendLog(
            'ESP32 connection successful'
        )

        updateStatusDisplay(
            status
        )

        startStatusPolling()

    } catch (error) {
        setConnectionState(
            false
        )

        appendLog(
            `Connection failed: ${error.message}`
        )
    }
}


function disconnectESP32() {
    stopStatusPolling()

    setConnectionState(
        false
    )

    appendLog(
        'Disconnected'
    )
}


function startStatusPolling() {
    stopStatusPolling()

    state.statusTimer =
        setInterval(
            refreshStatus,
            1000
        )
}


function stopStatusPolling() {
    if (
        state.statusTimer
    ) {
        clearInterval(
            state.statusTimer
        )

        state.statusTimer =
            null
    }
}


async function refreshStatus() {
    if (!state.connected) {
        return
    }

    try {
        const response =
            await fetchESP32(
                '/api/status'
            )

        const status =
            await response.json()

        updateStatusDisplay(
            status
        )

    } catch {
        setConnectionState(
            false
        )

        stopStatusPolling()

        appendLog(
            'ESP32 connection lost'
        )
    }
}


function updateStatusDisplay(
    status
) {
    elements.temperature.textContent =
        status.temperature === null ||
        status.temperature === undefined
            ? '--'
            : `${Number(
                status.temperature
            ).toFixed(1)} °C`

    elements.humidity.textContent =
        status.humidity === null ||
        status.humidity === undefined
            ? '--'
            : `${Number(
                status.humidity
            ).toFixed(1)} %`

    elements.water.textContent =
        status.water
            ? 'ON'
            : 'OFF'

    elements.running.textContent =
        status.running
            ? 'RUNNING'
            : 'STOPPED'

    renderProgramDisplays(
        status
    )
}


function createPaletteBlock(
    element
) {
    element.addEventListener(
        'dragstart',
        event => {
            state.dragging.paletteType =
                element.dataset.blockType

            state.dragging.block =
                null

            event.dataTransfer.effectAllowed =
                'copy'

            event.dataTransfer.setData(
                'text/plain',
                element.dataset.blockType
            )
        }
    )

    element.addEventListener(
        'dragend',
        clearDragState
    )
}


function clearDragState() {
    document
        .querySelectorAll(
            '.drop-active'
        )
        .forEach(
            element =>
                element.classList.remove(
                    'drop-active'
                )
        )

    state.dragging.paletteType =
        null

    if (
        state.dragging.block
    ) {
        state.dragging.block.style.opacity =
            ''
    }

    state.dragging.block =
        null
}


function setupPaletteDrag() {
    document
        .querySelectorAll(
            '.palette-block'
        )
        .forEach(
            createPaletteBlock
        )
}


function setupWorkspaceDrag() {
    elements.workspace.addEventListener(
        'dragover',
        event => {
            event.preventDefault()

            const zone =
                event.target.closest(
                    '[data-drop-zone]'
                )

            if (zone) {
                zone.classList.add(
                    'drop-active'
                )

                return
            }

            elements.workspace.classList.add(
                'drop-active'
            )
        }
    )

    elements.workspace.addEventListener(
        'dragleave',
        event => {
            const zone =
                event.target.closest(
                    '[data-drop-zone]'
                )

            if (zone) {
                zone.classList.remove(
                    'drop-active'
                )
            } else {
                elements.workspace.classList.remove(
                    'drop-active'
                )
            }
        }
    )

    elements.workspace.addEventListener(
        'drop',
        handleDrop
    )
}


function getRootDropZone() {
    return elements.workspace.querySelector(
        ':scope > [data-drop-zone="root"]'
    )
}


function getDropZoneFromEvent(
    event
) {
    const directZone =
        event.target.closest(
            '[data-drop-zone]'
        )

    if (directZone) {
        return directZone
    }

    return getRootDropZone()
}


function isDescendant(
    parent,
    child
) {
    if (!parent || !child) {
        return false
    }

    return parent.contains(
        child
    )
}


function handleDrop(event) {
    event.preventDefault()

    const zone =
        getDropZoneFromEvent(
            event
        )

    if (!zone) {
        clearDragState()
        return
    }

    if (
        state.dragging.block
    ) {
        const draggedBlock =
            state.dragging.block

        const targetBlock =
            zone.closest(
                '[data-block-id]'
            )

        if (
            draggedBlock ===
            targetBlock
        ) {
            clearDragState()
            return
        }

        if (
            isDescendant(
                draggedBlock,
                zone
            )
        ) {
            clearDragState()
            return
        }

        zone.parentElement.insertBefore(
            draggedBlock,
            zone
        )
    }


    if (
        state.dragging.paletteType
    ) {
        const block =
            createBlock(
                state.dragging.paletteType
            )

        if (block) {
            zone.parentElement.insertBefore(
                block,
                zone
            )
        }
    }

    clearDragState()

    refreshBlockDragListeners()
    updateProgram()
}


function refreshBlockDragListeners() {
    elements.workspace
        .querySelectorAll(
            '[data-block-id]'
        )
        .forEach(
            block => {
                block.draggable =
                    block.dataset.blockType !==
                    'start'

                block.ondragstart =
                    event => {
                        if (
                            block.dataset.blockType ===
                            'start'
                        ) {
                            event.preventDefault()
                            return
                        }

                        state.dragging.block =
                            block

                        state.dragging.paletteType =
                            null

                        block.style.opacity =
                            '0.45'

                        event.dataTransfer.effectAllowed =
                            'move'

                        event.dataTransfer.setData(
                            'text/plain',
                            block.dataset.blockId
                        )
                    }

                block.ondragend =
                    clearDragState
            }
        )
}


function createBlock(
    type
) {
    const definitions = {
        start: {
            category: 'event',
            label: 'START'
        },

        time: {
            category: 'event',
            label: 'AT TIME'
        },

        daily: {
            category: 'event',
            label: 'EVERY DAY'
        },

        if: {
            category: 'control',
            label: 'IF'
        },

        'else-if': {
            category: 'control',
            label: 'ELSE IF'
        },

        else: {
            category: 'control',
            label: 'ELSE'
        },

        repeat: {
            category: 'control',
            label: 'REPEAT'
        },

        forever: {
            category: 'control',
            label: 'FOREVER'
        },

        while: {
            category: 'control',
            label: 'WHILE'
        },

        wait: {
            category: 'control',
            label: 'WAIT'
        },

        stop: {
            category: 'control',
            label: 'STOP'
        },

        humidity: {
            category: 'sensor',
            label: 'HUMIDITY'
        },

        temperature: {
            category: 'sensor',
            label: 'TEMPERATURE'
        },

        'water-on': {
            category: 'actuator',
            label: 'SW_WATER ON'
        },

        'water-off': {
            category: 'actuator',
            label: 'SW_WATER OFF'
        },

        compare: {
            category: 'logic',
            label: 'COMPARE'
        },

        and: {
            category: 'logic',
            label: 'AND'
        },

        or: {
            category: 'logic',
            label: 'OR'
        },

        not: {
            category: 'logic',
            label: 'NOT'
        },

        math: {
            category: 'logic',
            label: 'MATH'
        },

        'set-variable': {
            category: 'variable',
            label: 'SET VARIABLE'
        },

        'change-variable': {
            category: 'variable',
            label: 'CHANGE VARIABLE'
        },

        variable: {
            category: 'variable',
            label: 'VARIABLE'
        },

        display: {
            category: 'display',
            label: 'DISPLAY'
        }
    }

    const definition =
        definitions[type]

    if (!definition) {
        return null
    }

    const block =
        createElement(
            'article',
            `block block-${definition.category}`
        )

    block.dataset.blockType =
        type

    block.dataset.blockCategory =
        definition.category

    block.dataset.blockId =
        String(
            state.nextBlockId
        )

    state.nextBlockId += 1


    const header =
        createElement(
            'div',
            'block-header'
        )

    const label =
        createElement(
            'span',
            'block-label',
            definition.label
        )

    const actions =
        createElement(
            'div',
            'block-actions'
        )

    if (
        type !== 'start'
    ) {
        const deleteButton =
            createElement(
                'button',
                'block-action',
                '×'
            )

        deleteButton.type =
            'button'

        deleteButton.dataset.action =
            'delete-block'

        actions.append(
            deleteButton
        )
    }

    header.append(
        label,
        actions
    )

    block.append(
        header
    )


    const content =
        createElement(
            'div',
            'block-content'
        )

    block.append(
        content
    )


    buildBlockContent(
        block,
        type,
        content
    )

    return block
}


function buildBlockContent(
    block,
    type,
    content
) {
    if (
        type === 'time'
    ) {
        content.append(
            createElement(
                'span',
                'block-note',
                'Run at'
            ),
            createInput(
                'time',
                '08:00',
                'time'
            )
        )

        return
    }


    if (
        type === 'daily'
    ) {
        content.append(
            createElement(
                'span',
                'block-note',
                'Run every day'
            )
        )

        return
    }


    if (
        type === 'if' ||
        type === 'else-if' ||
        type === 'while'
    ) {
        addConditionControls(
            content
        )

        addBody(
            block,
            type === 'while'
                ? 'DO'
                : 'THEN'
        )

        return
    }


    if (
        type === 'else'
    ) {
        addBody(
            block,
            'THEN'
        )

        return
    }


    if (
        type === 'repeat'
    ) {
        const row =
            createElement(
                'div',
                'block-row'
            )

        row.append(
            createElement(
                'span',
                'block-note',
                'Repeat'
            ),
            createInput(
                'number',
                '5',
                'count'
            ),
            createElement(
                'span',
                'block-note',
                'times'
            )
        )

        content.append(
            row
        )

        addBody(
            block,
            'DO'
        )

        return
    }


    if (
        type === 'forever'
    ) {
        content.append(
            createElement(
                'span',
                'block-note',
                'Loop forever'
            )
        )

        addBody(
            block,
            'DO'
        )

        return
    }


    if (
        type === 'wait'
    ) {
        const row =
            createElement(
                'div',
                'block-row'
            )

        row.append(
            createElement(
                'span',
                'block-note',
                'Wait'
            ),
            createInput(
                'number',
                '1',
                'duration'
            ),
            createSelect(
                [
                    {
                        value: 'milliseconds',
                        label: 'ms'
                    },
                    {
                        value: 'seconds',
                        label: 'seconds'
                    },
                    {
                        value: 'minutes',
                        label: 'minutes'
                    }
                ],
                'seconds',
                'unit'
            )
        )

        content.append(
            row
        )

        return
    }


    if (
        type === 'stop'
    ) {
        content.append(
            createSelect(
                [
                    {
                        value: 'program',
                        label: 'Program'
                    },
                    {
                        value: 'loop',
                        label: 'Loop'
                    },
                    {
                        value: 'water',
                        label: 'Watering'
                    }
                ],
                'program',
                'mode'
            )
        )

        return
    }


    if (
        type === 'humidity' ||
        type === 'temperature'
    ) {
        content.append(
            createElement(
                'span',
                'block-note',
                'Sensor value'
            )
        )

        return
    }


    if (
        type === 'water-on' ||
        type === 'water-off'
    ) {
        return
    }


    if (
        type === 'compare'
    ) {
        addConditionControls(
            content
        )

        return
    }


    if (
        type === 'and' ||
        type === 'or'
    ) {
        content.append(
            createInput(
                'text',
                'conditionA',
                'left'
            ),
            createElement(
                'span',
                'block-note',
                type.toUpperCase()
            ),
            createInput(
                'text',
                'conditionB',
                'right'
            )
        )

        return
    }


    if (
        type === 'not'
    ) {
        content.append(
            createInput(
                'text',
                'condition',
                'value'
            )
        )

        return
    }


    if (
        type === 'math'
    ) {
        content.append(
            createInput(
                'number',
                '1',
                'left'
            ),
            createSelect(
                [
                    {
                        value: '+',
                        label: '+'
                    },
                    {
                        value: '-',
                        label: '-'
                    },
                    {
                        value: '*',
                        label: '×'
                    },
                    {
                        value: '/',
                        label: '÷'
                    },
                    {
                        value: '%',
                        label: 'MOD'
                    }
                ],
                '+',
                'operator'
            ),
            createInput(
                'number',
                '1',
                'right'
            )
        )

        return
    }


    if (
        type === 'set-variable' ||
        type === 'change-variable'
    ) {
        content.append(
            createVariableSelect(
                'variable'
            ),
            createInput(
                'text',
                '0',
                'value'
            )
        )

        return
    }


    if (
        type === 'variable'
    ) {
        content.append(
            createVariableSelect(
                'variable'
            )
        )

        return
    }


    if (
        type === 'display'
    ) {
        const row =
            createElement(
                'div',
                'block-row'
            )

        const label =
            createInput(
                'text',
                'My Display',
                'label'
            )

        const source =
            createSelect(
                [
                    {
                        value: 'text',
                        label: 'Text'
                    },
                    {
                        value: 'temperature',
                        label: 'Temperature'
                    },
                    {
                        value: 'humidity',
                        label: 'Humidity'
                    },
                    {
                        value: 'water',
                        label: 'Water'
                    },
                    {
                        value: 'variable',
                        label: 'Variable'
                    }
                ],
                'temperature',
                'value-source'
            )

        const value =
            createInput(
                'text',
                'Hello',
                'value'
            )

        value.dataset.displayValue =
            'true'

        row.append(
            createElement(
                'span',
                'block-note',
                'Label'
            ),
            label,
            createElement(
                'span',
                'block-note',
                'Value'
            ),
            source,
            value
        )

        content.append(
            row
        )

        source.addEventListener(
            'change',
            () => {
                updateDisplayInput(
                    block
                )

                updateProgram()
            }
        )

        updateDisplayInput(
            block
        )

        return
    }
}


function addConditionControls(
    container
) {
    const row =
        createElement(
            'div',
            'block-row'
        )

    row.append(
        createSelect(
            [
                {
                    value: 'humidity',
                    label: 'Humidity'
                },
                {
                    value: 'temperature',
                    label: 'Temperature'
                }
            ],
            'humidity',
            'sensor'
        ),
        createSelect(
            [
                {
                    value: '<',
                    label: '<'
                },
                {
                    value: '>',
                    label: '>'
                },
                {
                    value: '<=',
                    label: '≤'
                },
                {
                    value: '>=',
                    label: '≥'
                },
                {
                    value: '==',
                    label: '='
                },
                {
                    value: '!=',
                    label: '≠'
                }
            ],
            '<',
            'operator'
        ),
        createInput(
            'number',
            '60',
            'value'
        )
    )

    container.append(
        row
    )
}


function addBody(
    block,
    labelText
) {
    const body =
        createElement(
            'div',
            'block-body'
        )

    body.dataset.blockBody =
        'true'

    const label =
        createElement(
            'div',
            'block-body-label',
            labelText
        )

    const zone =
        createElement(
            'div',
            'drop-zone'
        )

    zone.dataset.dropZone =
        'nested'

    body.append(
        label,
        zone
    )

    block.append(
        body
    )

    return body
}


function createInput(
    type,
    value,
    field
) {
    const input =
        createElement(
            'input',
            'block-input'
        )

    input.type =
        type

    input.value =
        value

    input.dataset.field =
        field

    return input
}


function createSelect(
    options,
    value,
    field
) {
    const select =
        createElement(
            'select',
            'block-select'
        )

    select.dataset.field =
        field

    for (
        const option of options
    ) {
        const item =
            createElement(
                'option'
            )

        item.value =
            option.value

        item.textContent =
            option.label

        if (
            option.value === value
        ) {
            item.selected =
                true
        }

        select.append(
            item
        )
    }

    return select
}


function createVariableSelect(
    field
) {
    const options =
        state.variables.map(
            variable => ({
                value: variable,
                label: variable
            })
        )

    if (
        options.length === 0
    ) {
        options.push({
            value: '',
            label: 'No variables'
        })
    }

    return createSelect(
        options,
        options[0].value,
        field
    )
}


function updateDisplayInput(
    block
) {
    const source =
        block.querySelector(
            '[data-field="value-source"]'
        )

    const value =
        block.querySelector(
            '[data-display-value="true"]'
        )

    if (
        !source ||
        !value
    ) {
        return
    }

    const sourceType =
        source.value

    if (
        sourceType === 'text'
    ) {
        value.readOnly =
            false

        value.placeholder =
            'Enter text'

        if (
            value.dataset.generated ===
            'true'
        ) {
            value.value =
                'Hello'

            value.dataset.generated =
                'false'
        }

        return
    }

    value.value =
        sourceType

    value.readOnly =
        true

    value.dataset.generated =
        'true'
}


function blockField(
    block,
    field
) {
    return block.querySelector(
        `[data-field="${field}"]`
    )
}


function durationToMilliseconds(
    block
) {
    const amount =
        Number(
            blockField(
                block,
                'duration'
            )?.value || 0
        )

    const unit =
        blockField(
            block,
            'unit'
        )?.value ||
        'seconds'

    if (
        unit === 'milliseconds'
    ) {
        return amount
    }

    if (
        unit === 'minutes'
    ) {
        return amount * 60000
    }

    return amount * 1000
}


function readNestedSteps(
    block
) {
    const body =
        block.querySelector(
            ':scope > .block-body'
        )

    if (!body) {
        return []
    }

    return Array.from(
        body.querySelectorAll(
            ':scope > [data-block-id]'
        )
    ).map(
        blockToStep
    )
}


function blockToStep(
    block
) {
    const type =
        block.dataset.blockType


    if (
        type === 'start'
    ) {
        return {
            type: 'start'
        }
    }


    if (
        type === 'water-on'
    ) {
        return {
            type: 'water',
            state: true
        }
    }


    if (
        type === 'water-off'
    ) {
        return {
            type: 'water',
            state: false
        }
    }


    if (
        type === 'humidity'
    ) {
        return {
            type: 'sensor',
            sensor: 'humidity'
        }
    }


    if (
        type === 'temperature'
    ) {
        return {
            type: 'sensor',
            sensor: 'temperature'
        }
    }


    if (
        type === 'wait'
    ) {
        return {
            type: 'wait',
            ms:
                durationToMilliseconds(
                    block
                )
        }
    }


    if (
        type === 'time'
    ) {
        return {
            type: 'time',
            time:
                blockField(
                    block,
                    'time'
                )?.value ||
                '08:00'
        }
    }


    if (
        type === 'daily'
    ) {
        return {
            type: 'daily'
        }
    }


    if (
        type === 'if' ||
        type === 'else-if' ||
        type === 'while'
    ) {
        return {
            type,
            condition: {
                sensor:
                    blockField(
                        block,
                        'sensor'
                    )?.value ||
                    'humidity',

                operator:
                    blockField(
                        block,
                        'operator'
                    )?.value ||
                    '<',

                value:
                    Number(
                        blockField(
                            block,
                            'value'
                        )?.value ||
                        0
                    )
            },

            steps:
                readNestedSteps(
                    block
                )
        }
    }


    if (
        type === 'else' ||
        type === 'forever'
    ) {
        return {
            type,
            steps:
                readNestedSteps(
                    block
                )
        }
    }


    if (
        type === 'repeat'
    ) {
        return {
            type,
            count:
                Number(
                    blockField(
                        block,
                        'count'
                    )?.value ||
                    0
                ),

            steps:
                readNestedSteps(
                    block
                )
        }
    }


    if (
        type === 'stop'
    ) {
        return {
            type,
            mode:
                blockField(
                    block,
                    'mode'
                )?.value ||
                'program'
        }
    }


    if (
        type === 'compare'
    ) {
        return {
            type,
            sensor:
                blockField(
                    block,
                    'sensor'
                )?.value ||
                'humidity',

            operator:
                blockField(
                    block,
                    'operator'
                )?.value ||
                '<',

            value:
                Number(
                    blockField(
                        block,
                        'value'
                    )?.value ||
                    0
                )
        }
    }


    if (
        type === 'and' ||
        type === 'or'
    ) {
        return {
            type,
            left:
                blockField(
                    block,
                    'left'
                )?.value ||
                '',

            right:
                blockField(
                    block,
                    'right'
                )?.value ||
                ''
        }
    }


    if (
        type === 'not'
    ) {
        return {
            type,
            value:
                blockField(
                    block,
                    'value'
                )?.value ||
                ''
        }
    }


    if (
        type === 'math'
    ) {
        return {
            type,
            left:
                Number(
                    blockField(
                        block,
                        'left'
                    )?.value ||
                    0
                ),

            operator:
                blockField(
                    block,
                    'operator'
                )?.value ||
                '+',

            right:
                Number(
                    blockField(
                        block,
                        'right'
                    )?.value ||
                    0
                )
        }
    }


    if (
        type === 'set-variable'
    ) {
        return {
            type,
            variable:
                blockField(
                    block,
                    'variable'
                )?.value ||
                '',

            value:
                blockField(
                    block,
                    'value'
                )?.value ||
                ''
        }
    }


    if (
        type === 'change-variable'
    ) {
        return {
            type,
            variable:
                blockField(
                    block,
                    'variable'
                )?.value ||
                '',

            amount:
                blockField(
                    block,
                    'value'
                )?.value ||
                '0'
        }
    }


    if (
        type === 'variable'
    ) {
        return {
            type,
            variable:
                blockField(
                    block,
                    'variable'
                )?.value ||
                ''
        }
    }


    if (
        type === 'display'
    ) {
        const source =
            blockField(
                block,
                'value-source'
            )?.value ||
            'text'

        let value

        if (
            source === 'text'
        ) {
            value = {
                source: 'text',
                text:
                    blockField(
                        block,
                        'value'
                    )?.value ||
                    ''
            }
        } else {
            value = {
                source
            }
        }

        return {
            type: 'display',
            label:
                blockField(
                    block,
                    'label'
                )?.value ||
                'Display',

            value
        }
    }


    return {
        type
    }
}


function getRootBlocks() {
    return Array.from(
        elements.workspace.querySelectorAll(
            ':scope > [data-block-id]'
        )
    )
}


function updateProgram() {
    state.program.name =
        elements.programName.value.trim() ||
        'My Program'

    state.program.steps =
        getRootBlocks().map(
            blockToStep
        )

    saveLocalProgram()

    renderProgramDisplays()
}


function renderProgramDisplays(
    status = null
) {
    elements.displayGrid.replaceChildren()

    const displays =
        collectDisplayBlocks(
            state.program.steps
        )

    if (
        displays.length === 0
    ) {
        elements.displayGrid.append(
            createElement(
                'div',
                'empty-message',
                'Add a DISPLAY block in the Editor tab'
            )
        )

        return
    }

    for (
        const display of displays
    ) {
        const card =
            createElement(
                'article',
                'user-display-card'
            )

        const label =
            createElement(
                'span',
                'user-display-label',
                display.label
            )

        const value =
            createElement(
                'strong',
                'user-display-value'
            )

        value.textContent =
            resolveDisplayValue(
                display.value,
                status
            )

        card.append(
            label,
            value
        )

        elements.displayGrid.append(
            card
        )
    }
}


function collectDisplayBlocks(
    steps
) {
    const displays = []

    for (
        const step of steps
    ) {
        if (
            step.type === 'display'
        ) {
            displays.push(
                step
            )
        }

        if (
            Array.isArray(
                step.steps
            )
        ) {
            displays.push(
                ...collectDisplayBlocks(
                    step.steps
                )
            )
        }
    }

    return displays
}


function resolveDisplayValue(
    value,
    status
) {
    if (
        !value
    ) {
        return '--'
    }

    if (
        value.source === 'text'
    ) {
        return value.text || ''
    }

    if (
        !status
    ) {
        return '--'
    }

    if (
        value.source === 'temperature'
    ) {
        return status.temperature === null ||
        status.temperature === undefined
            ? '--'
            : `${Number(
                status.temperature
            ).toFixed(1)} °C`
    }

    if (
        value.source === 'humidity'
    ) {
        return status.humidity === null ||
        status.humidity === undefined
            ? '--'
            : `${Number(
                status.humidity
            ).toFixed(1)} %`
    }

    if (
        value.source === 'water'
    ) {
        return status.water
            ? 'ON'
            : 'OFF'
    }

    if (
        value.source === 'variable'
    ) {
        return '--'
    }

    return '--'
}


function saveLocalProgram() {
    try {
        localStorage.setItem(
            PROGRAM_STORAGE_KEY,
            JSON.stringify(
                state.program
            )
        )

        localStorage.setItem(
            VARIABLES_STORAGE_KEY,
            JSON.stringify(
                state.variables
            )
        )
    } catch (error) {
        appendLog(
            `Local save failed: ${error.message}`
        )
    }
}


function loadLocalProgram() {
    try {
        const savedProgram =
            localStorage.getItem(
                PROGRAM_STORAGE_KEY
            )

        const savedVariables =
            localStorage.getItem(
                VARIABLES_STORAGE_KEY
            )

        if (
            savedVariables
        ) {
            const variables =
                JSON.parse(
                    savedVariables
                )

            if (
                Array.isArray(
                    variables
                )
            ) {
                state.variables =
                    variables
            }
        }

        if (
            savedProgram
        ) {
            const program =
                JSON.parse(
                    savedProgram
                )

            if (
                program &&
                Array.isArray(
                    program.steps
                )
            ) {
                state.program =
                    program
            }
        }
    } catch (error) {
        state.program = {
            name: 'My Program',
            steps: []
        }

        state.variables = []

        appendLog(
            `Local data reset: ${error.message}`
        )
    }
}


function renderProgram(
    program
) {
    elements.workspace
        .querySelectorAll(
            ':scope > [data-block-id]'
        )
        .forEach(
            block =>
                block.remove()
        )

    elements.programName.value =
        program.name ||
        'My Program'

    for (
        const step of program.steps || []
    ) {
        const block =
            stepToBlock(
                step
            )

        if (
            block
        ) {
            const zone =
                getRootDropZone()

            if (
                zone
            ) {
                zone.parentElement.insertBefore(
                    block,
                    zone
                )
            }
        }
    }

    refreshBlockDragListeners()
}


function stepToBlock(
    step
) {
    const type =
        step.type === 'water'
            ? step.state
                ? 'water-on'
                : 'water-off'
            : step.type

    const block =
        createBlock(
            type
        )

    if (
        !block
    ) {
        return null
    }


    if (
        type === 'time'
    ) {
        const field =
            blockField(
                block,
                'time'
            )

        if (
            field
        ) {
            field.value =
                step.time ||
                '08:00'
        }
    }


    if (
        type === 'wait'
    ) {
        const duration =
            blockField(
                block,
                'duration'
            )

        const unit =
            blockField(
                block,
                'unit'
            )

        if (
            duration
        ) {
            duration.value =
                step.ms / 1000
        }

        if (
            unit
        ) {
            unit.value =
                'seconds'
        }
    }


    if (
        type === 'display'
    ) {
        const label =
            blockField(
                block,
                'label'
            )

        const source =
            blockField(
                block,
                'value-source'
            )

        const value =
            blockField(
                block,
                'value'
            )

        if (
            label
        ) {
            label.value =
                step.label ||
                'Display'
        }

        if (
            step.value &&
            source
        ) {
            source.value =
                step.value.source ||
                'text'
        }

        if (
            step.value &&
            value
        ) {
            value.value =
                step.value.source ===
                'text'
                    ? step.value.text || ''
                    : step.value.source
        }

        updateDisplayInput(
            block
        )
    }


    if (
        type === 'if' ||
        type === 'else-if' ||
        type === 'while'
    ) {
        setConditionValues(
            block,
            step.condition
        )

        renderNestedSteps(
            block,
            step.steps || []
        )
    }


    if (
        type === 'else' ||
        type === 'forever'
    ) {
        renderNestedSteps(
            block,
            step.steps || []
        )
    }


    if (
        type === 'repeat'
    ) {
        const count =
            blockField(
                block,
                'count'
            )

        if (
            count
        ) {
            count.value =
                String(
                    step.count ??
                    1
                )
        }

        renderNestedSteps(
            block,
            step.steps || []
        )
    }


    return block
}


function setConditionValues(
    block,
    condition
) {
    if (
        !condition
    ) {
        return
    }

    const sensor =
        blockField(
            block,
            'sensor'
        )

    const operator =
        blockField(
            block,
            'operator'
        )

    const value =
        blockField(
            block,
            'value'
        )

    if (
        sensor
    ) {
        sensor.value =
            condition.sensor ||
            'humidity'
    }

    if (
        operator
    ) {
        operator.value =
            condition.operator ||
            '<'
    }

    if (
        value
    ) {
        value.value =
            String(
                condition.value ??
                0
            )
    }
}


function renderNestedSteps(
    block,
    steps
) {
    const body =
        block.querySelector(
            ':scope > .block-body'
        )

    if (
        !body
    ) {
        return
    }

    const zone =
        body.querySelector(
            ':scope > [data-drop-zone]'
        )

    if (
        !zone
    ) {
        return
    }

    body
        .querySelectorAll(
            ':scope > [data-block-id]'
        )
        .forEach(
            child =>
                child.remove()
        )

    for (
        const step of steps
    ) {
        const child =
            stepToBlock(
                step
            )

        if (
            child
        ) {
            zone.parentElement.insertBefore(
                child,
                zone
            )
        }
    }
}


function renderVariables() {
    elements.variableList.replaceChildren()

    if (
        state.variables.length === 0
    ) {
        elements.variableList.append(
            createElement(
                'div',
                'empty-message',
                'No variables'
            )
        )

        return
    }

    for (
        const variable of state.variables
    ) {
        const item =
            createElement(
                'div',
                'variable-item'
            )

        const name =
            createElement(
                'span',
                'variable-name',
                variable
            )

        const remove =
            createElement(
                'button',
                'variable-delete',
                'Delete'
            )

        remove.type =
            'button'

        remove.dataset.action =
            'delete-variable'

        remove.dataset.variable =
            variable

        item.append(
            name,
            remove
        )

        elements.variableList.append(
            item
        )
    }
}


function refreshVariableBlocks() {
    elements.workspace
        .querySelectorAll(
            '[data-field="variable"]'
        )
        .forEach(
            oldSelect => {
                const current =
                    oldSelect.value

                const replacement =
                    createVariableSelect(
                        'variable'
                    )

                if (
                    state.variables.includes(
                        current
                    )
                ) {
                    replacement.value =
                        current
                }

                oldSelect.replaceWith(
                    replacement
                )
            }
        )
}


function createVariable(
    rawName
) {
    const name =
        rawName
            .trim()
            .replace(
                /\s+/g,
                '_'
            )

    if (
        !name
    ) {
        return
    }

    if (
        state.variables.includes(
            name
        )
    ) {
        return
    }

    state.variables.push(
        name
    )

    renderVariables()
    refreshVariableBlocks()
    saveLocalProgram()
}


function deleteVariable(
    name
) {
    state.variables =
        state.variables.filter(
            variable =>
                variable !== name
        )

    renderVariables()
    refreshVariableBlocks()
    saveLocalProgram()
}


async function saveProgramToESP32() {
    if (
        !state.connected
    ) {
        appendLog(
            'Connect to the ESP32 first'
        )

        return
    }

    updateProgram()

    try {
        const response =
            await fetchESP32(
                '/api/program',
                {
                    method: 'POST',

                    headers: {
                        'Content-Type':
                            'application/json'
                    },

                    body:
                        JSON.stringify(
                            state.program
                        )
                }
            )

        await response.json()

        appendLog(
            'Program saved to ESP32'
        )

    } catch (error) {
        appendLog(
            `Save failed: ${error.message}`
        )
    }
}


async function loadProgramFromESP32() {
    if (
        !state.connected
    ) {
        appendLog(
            'Connect to the ESP32 first'
        )

        return
    }

    try {
        const response =
            await fetchESP32(
                '/api/program'
            )

        const result =
            await response.json()

        if (
            !result.program
        ) {
            throw new Error(
                'ESP32 returned no program'
            )
        }

        state.program =
            result.program

        renderProgram(
            state.program
        )

        saveLocalProgram()
        renderProgramDisplays()

        appendLog(
            'Program loaded from ESP32'
        )

    } catch (error) {
        appendLog(
            `Load failed: ${error.message}`
        )
    }
}


async function uploadProgram() {
    await saveProgramToESP32()
}


async function runProgram() {
    if (
        !state.connected
    ) {
        appendLog(
            'Connect to the ESP32 first'
        )

        return
    }

    try {
        await fetchESP32(
            '/api/run',
            {
                method: 'POST'
            }
        )

        appendLog(
            'Program started'
        )

    } catch (error) {
        appendLog(
            `Run failed: ${error.message}`
        )
    }
}


async function stopProgram() {
    if (
        !state.connected
    ) {
        appendLog(
            'Connect to the ESP32 first'
        )

        return
    }

    try {
        await fetchESP32(
            '/api/stop',
            {
                method: 'POST'
            }
        )

        appendLog(
            'Program stopped'
        )

    } catch (error) {
        appendLog(
            `Stop failed: ${error.message}`
        )
    }
}


function newProgram() {
    state.nextBlockId =
        1

    state.program = {
        name: 'My Program',
        steps: []
    }

    elements.programName.value =
        'My Program'

    elements.workspace
        .querySelectorAll(
            ':scope > [data-block-id]'
        )
        .forEach(
            block =>
                block.remove()
        )

    saveLocalProgram()
    renderProgramDisplays()

    appendLog(
        'New program created'
    )
}


function clearWorkspace() {
    elements.workspace
        .querySelectorAll(
            ':scope > [data-block-id]'
        )
        .forEach(
            block =>
                block.remove()
        )

    updateProgram()

    appendLog(
        'Workspace cleared'
    )
}


function switchTab(
    tab
) {
    document
        .querySelectorAll(
            '[data-tab]'
        )
        .forEach(
            button => {
                button.classList.toggle(
                    'tab-active',
                    button.dataset.tab ===
                    tab
                )
            }
        )

    document
        .querySelectorAll(
            '[data-page]'
        )
        .forEach(
            page => {
                page.classList.toggle(
                    'tab-page-active',
                    page.dataset.page ===
                    tab
                )
            }
        )

    if (
        tab === 'display'
    ) {
        renderProgramDisplays()
        refreshStatus()
    }
}


function attachActions() {
    document.addEventListener(
        'click',
        event => {
            const action =
                event.target.closest(
                    '[data-action]'
                )

            if (
                action
            ) {
                const name =
                    action.dataset.action

                if (
                    name === 'connect'
                ) {
                    connectESP32()
                }

                if (
                    name === 'disconnect'
                ) {
                    disconnectESP32()
                }

                if (
                    name === 'new-program'
                ) {
                    newProgram()
                }

                if (
                    name === 'load-program'
                ) {
                    loadProgramFromESP32()
                }

                if (
                    name === 'save-program'
                ) {
                    saveProgramToESP32()
                }

                if (
                    name === 'upload-program'
                ) {
                    uploadProgram()
                }

                if (
                    name === 'run-program'
                ) {
                    runProgram()
                }

                if (
                    name === 'stop-program'
                ) {
                    stopProgram()
                }

                if (
                    name === 'clear-workspace'
                ) {
                    clearWorkspace()
                }

                if (
                    name === 'clear-monitor'
                ) {
                    clearLog()
                }

                if (
                    name === 'create-variable'
                ) {
                    createVariable(
                        elements.newVariableName.value
                    )

                    elements.newVariableName.value =
                        ''
                }

                if (
                    name === 'delete-variable'
                ) {
                    deleteVariable(
                        action.dataset.variable
                    )
                }

                if (
                    name === 'delete-block'
                ) {
                    const block =
                        action.closest(
                            '[data-block-id]'
                        )

                    if (
                        block
                    ) {
                        block.remove()
                        updateProgram()
                    }
                }
            }


            const tab =
                event.target.closest(
                    '[data-tab]'
                )

            if (
                tab
            ) {
                switchTab(
                    tab.dataset.tab
                )
            }
        }
    )


    elements.programName.addEventListener(
        'input',
        updateProgram
    )


    elements.workspace.addEventListener(
        'input',
        updateProgram
    )


    elements.workspace.addEventListener(
        'change',
        updateProgram
    )
}


function initialise() {
    loadLocalProgram()

    renderVariables()

    renderProgram(
        state.program
    )

    setupPaletteDrag()
    setupWorkspaceDrag()
    attachActions()

    setConnectionState(
        false
    )

    appendLog(
        'Visual Automation Studio ready'
    )

    appendLog(
        'Connect to ESP32_Smart_Seedling_Waterer over Wi-Fi'
    )
}


initialise()