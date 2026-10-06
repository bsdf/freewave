import QtQuick

// Concept 01 "Ambient Wash": full-bleed art-color radial field, floating cover,
// oversized title, glanceable hairline progress. Display-only. Binds to the `np`
// context object (NowPlayingModel).
Item {
    id: root

    // Reusable styled Text types (QML's "CSS class") — shared defaults set once,
    // call sites only carry per-instance overrides.
    component MonoText: Text {
        renderType: Text.NativeRendering
        font.family: Theme.monoFamily
        color: Theme.textColor
    }
    component SansText: Text {
        renderType: Text.NativeRendering
        font.family: Theme.sansFamily
        color: Theme.textColor
    }

    // Reusable GPU visualizer host: feeds every fragment-shader visualization the
    // same uniform set (accent, 32 bands packed as eight vec4, level, time,
    // resolution) so screens are swappable by changing `shader` alone. Driven at
    // refresh rate by FrameAnimation; hidden when no visualizer data is available.
    component Visualizer: ShaderEffect {
        id: vis
        property string shader
        fragmentShader: shader
        blending: true
        visible: np.bandCount > 0

        // Live spectrum + meta — the always-available inputs.
        property color accent: np.accent
        property real level: np.level
        property real washEnergy: np.washEnergy // slow loudness envelope (ambient)
        property real washBass: np.washBass     // slow bass envelope (ambient)
        property real time: 0
        // Music-paced clock for ambient washes: integrates a loudness-swelled
        // speed rather than adding energy to the phase, so the flow speeds up
        // and eases but never runs backwards when the envelope decays.
        property real flowTime: 0
        // Re-seeded on every track change so the aurora/auroracalm noise field
        // doesn't replay the same pose (flowTime/time both begin at 0 at scene
        // creation and, for auroracalm, on every play/pause toggle too). The
        // shaders cross-fade from prevPhaseOffset to phaseOffset over
        // phaseBlend rather than jump-cutting to the new pose.
        property real phaseOffset: Math.random() * 1000.0
        property real prevPhaseOffset: phaseOffset
        property real phaseBlend: 1.0

        NumberAnimation {
            id: phaseFade
            target: vis
            property: "phaseBlend"
            from: 0.0
            to: 1.0
            duration: 2500
            easing.type: Easing.InOutQuad
        }

        Connections {
            target: np
            function onTrackChanged() {
                vis.prevPhaseOffset = vis.phaseOffset
                vis.phaseOffset = Math.random() * 1000.0
                phaseFade.restart()
            }
        }

        property int head: np.vizHead   // committed history rows (ring-buffer head)
        property real frac: np.vizFrac  // sub-row scroll position 0..1
        property int histRows: np.historyRows // history texture height (ring modulo)
        property vector2d resolution: Qt.vector2d(width, height)

        function band(i) { return np.bands[i] || 0 }
        property vector4d b0: Qt.vector4d(band(0),  band(1),  band(2),  band(3))
        property vector4d b1: Qt.vector4d(band(4),  band(5),  band(6),  band(7))
        property vector4d b2: Qt.vector4d(band(8),  band(9),  band(10), band(11))
        property vector4d b3: Qt.vector4d(band(12), band(13), band(14), band(15))
        property vector4d b4: Qt.vector4d(band(16), band(17), band(18), band(19))
        property vector4d b5: Qt.vector4d(band(20), band(21), band(22), band(23))
        property vector4d b6: Qt.vector4d(band(24), band(25), band(26), band(27))
        property vector4d b7: Qt.vector4d(band(28), band(29), band(30), band(31))

        // Scrolling-spectrogram history texture (bands × rows, newest at row 0).
        // A standard sampler input; shaders that don't draw history just ignore it.
        property variant history: histTex
        Image {
            id: histTex
            source: "image://fwvizhist/" + np.historyRev
            asynchronous: false
            cache: false
            smooth: true
            visible: false
        }

        // Album-art texture — a standard sampler input for art-reactive shaders;
        // others ignore it. Reloads when the track's coverId changes.
        property variant cover: coverTex
        Image {
            id: coverTex
            source: "image://fwnp/" + np.coverId
            sourceSize: Qt.size(512, 512)
            asynchronous: true
            cache: false
            smooth: true
            visible: false
        }

        FrameAnimation {
            running: vis.visible
            onTriggered: {
                vis.time = elapsedTime
                vis.flowTime += frameTime * (1.0 + 1.5 * np.washEnergy)
            }
        }
    }

    // Linear RGB blend → color. `t` parts of c1, the rest c2.
    function mix(c1, t, c2) {
        c1 = Qt.color(c1); c2 = Qt.color(c2)
        return Qt.rgba(c1.r * t + c2.r * (1 - t),
                       c1.g * t + c2.g * (1 - t),
                       c1.b * t + c2.b * (1 - t), 1.0)
    }
    function fmtTime(sec) {
        if (sec < 0) sec = 0
        var m = Math.floor(sec / 60)
        var s = sec % 60
        return m + ":" + (s < 10 ? "0" + s : s)
    }

    // Registry of pickable visualizations. `presetId` is the stable key persisted
    // in settings (via np.vizPreset); `fill` chooses full-bleed vs. the inset
    // "Unknown Pleasures" framing. The static wash.frag is the no-viz background,
    // not a preset. albumwash, coverfield, ridgeline (inset) and waterfall are
    // built but not offered; the shader lab still previews them.
    ListModel {
        id: vizPresets
        ListElement { presetId: "aurora";     name: "Aurora"; shader: "qrc:/shaders/aurora.frag.qsb";     fill: true }
        ListElement { presetId: "auroracalm"; name: "Calm";   shader: "qrc:/shaders/auroracalm.frag.qsb"; fill: true }
    }

    function vizIndexOf(id) {
        for (var i = 0; i < vizPresets.count; i++)
            if (vizPresets.get(i).presetId === id) return i
        return 0
    }

    // ── Scene-wide activity watcher. In fullscreen, any mouse movement over the
    // scene pokes NowPlayingView so it can auto-hide the cursor after inactivity.
    // Lowest in the stack + Qt.NoButton so it never intercepts the controls' hover
    // or clicks; hover tracking is gated on fullscreen so it costs nothing windowed.
    MouseArea {
        id: activityWatcher
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: np.fullscreen
        onPositionChanged: np.pokeActivity()
    }

    // ── Art-color field: radial gradient anchored top-left, blended to black.
    // GPU shader computes it in float and dithers at output, so the dark field
    // shows no 8-bit banding. Stop colors derive from the album accent.
    ShaderEffect {
        anchors.fill: parent
        // Hot-reload hook: the shader lab harness injects a `labShader` context
        // object and points `washPath` at a freshly baked .qsb to preview edits
        // live. Absent in the app → undefined → the baked-in shader.
        fragmentShader: (typeof labShader !== "undefined" && labShader.washPath)
            ? labShader.washPath : "qrc:/shaders/wash.frag.qsb"
        property color color0: root.mix(np.accent, 0.72, Theme.washNear)
        property color color1: root.mix(np.accent, 0.32, Theme.washMid)
        property color color2: Theme.washFar
        property vector2d resolution: Qt.vector2d(width, height)
    }

    // ── Visualizer backdrop. Above the wash and below the hero. Inset into a
    // centered, contained block (margin around it) so the ridgeline reads like the
    // framed plot on the "Unknown Pleasures" sleeve rather than bleeding to the
    // edges. Swap `shader` to change the visualization (all share the uniform set).
    Visualizer {
        id: viz
        property int presetIdx: root.vizIndexOf(np.vizPreset)
        // Hot-reload hook (see the wash ShaderEffect above): the shader lab
        // overrides `vizPath` with a freshly baked .qsb and picks the layout via
        // `vizInset`; undefined in the app.
        property bool fill: typeof labShader !== "undefined"
            ? !labShader.vizInset : vizPresets.get(presetIdx).fill
        shader: (typeof labShader !== "undefined" && labShader.vizPath)
            ? labShader.vizPath : vizPresets.get(presetIdx).shader
        // Full-bleed presets fill the field; non-fill ones (ridgeline) sit in a
        // centered inset block with margin around them (the UP framing).
        anchors.centerIn: parent
        width: fill ? parent.width : Math.min(parent.width * 0.9, 1500)
        height: fill ? parent.height : Math.min(parent.height * 0.88, 980)
        visible: np.visualizerEnabled && np.bandCount > 0
    }

    // ── Close affordance, top-left. Hidden in fullscreen: the user must leave
    // fullscreen (Esc / F / F11 / the corner button) before navigating away, so
    // the app can't end up showing another screen with the chrome still hidden.
    // Same chevron asset + icon size as the widget back button
    // (theme::ui::back_button's chevron-left.svg at 12x12), but text+icon only
    // — no chip/outline — to match the other NP corner controls (vizBtn, fsBtn).
    // x/y land the chevron at the exact same mainarea coordinate as every other
    // screen's back button: those sit in a 28px-margin container, and the widget
    // button's own border(1)+left-padding(8) insets the glyph a further 9px, so
    // 28+9 = 37. y:20 already compensates for that button's top border+padding.
    Item {
        id: backBtn
        visible: !np.fullscreen
        x: 37; y: 20
        width: backRow.implicitWidth
        height: backRow.implicitHeight
        opacity: backMouse.containsMouse ? 1.0 : 0.62
        Behavior on opacity { NumberAnimation { duration: 120 } }

        Row {
            id: backRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Image {
                id: backChevron
                source: "qrc:/icons/chevron-left-light.svg"
                sourceSize: Qt.size(12, 12)
                width: 12; height: 12
                opacity: 0.85
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -0.5
            }
            MonoText {
                anchors.verticalCenter: parent.verticalCenter
                text: "LIBRARY"
                color: Theme.textAlpha(0.85)
                font.pixelSize: 10
                font.weight: Font.Medium
                font.letterSpacing: 1.5
            }
        }
        MouseArea {
            id: backMouse
            anchors.fill: parent
            anchors.margins: -10
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: np.requestClose()
        }
    }

    // ── Visualizer control, top-right. Only offered when a visualizer feed is
    // available. Two hit zones: the filled/hollow dot toggles on/off; the name
    // cycles to the next visualization (and turns it on if it was off). Off by
    // default.
    Row {
        id: vizBtn
        visible: np.bandCount > 0
        anchors.right: parent.right
        anchors.rightMargin: 28
        y: 20
        spacing: 7

        MonoText {
            id: vizDot
            anchors.verticalCenter: parent.verticalCenter
            text: np.visualizerEnabled ? "●" : "○"
            color: Theme.textAlpha(np.visualizerEnabled ? 0.82 : 0.5)
            font.pixelSize: 10
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: np.visualizerEnabled = !np.visualizerEnabled
            }
        }
        MonoText {
            id: vizName
            anchors.verticalCenter: parent.verticalCenter
            text: np.visualizerEnabled
                ? vizPresets.get(viz.presetIdx).name.toUpperCase() + "  ›"
                : "VISUALIZER"
            color: Theme.textAlpha(np.visualizerEnabled ? 0.82 : 0.5)
            font.pixelSize: 10
            font.letterSpacing: 1.5
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (!np.visualizerEnabled) { np.visualizerEnabled = true; return }
                    var next = (viz.presetIdx + 1) % vizPresets.count
                    np.vizPreset = vizPresets.get(next).presetId
                }
            }
        }
    }

    // ── Fullscreen toggle, bottom-right. Drawn as corner brackets (a Canvas, so
    // it renders crisply regardless of the mono font's glyph coverage): brackets
    // point outward to enter fullscreen, inward to exit. MainWindow owns the
    // window-state change (np.fullscreen is driven back from there).
    Canvas {
        id: fsBtn
        width: 18; height: 18
        anchors.right: parent.right
        anchors.rightMargin: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        opacity: fsHover.containsMouse ? 1.0 : 0.62
        Behavior on opacity { NumberAnimation { duration: 120 } }

        property bool exit: np.fullscreen
        property color stroke: Theme.textAlpha(0.85)
        onExitChanged: requestPaint()
        onStrokeChanged: requestPaint()
        Component.onCompleted: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = stroke
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            var w = width, h = height
            var arm = 5           // bracket arm length
            var inset = exit ? 5 : 1.5  // inward for "exit", hugging edges for "enter"
            var x0 = inset, y0 = inset, x1 = w - inset, y1 = h - inset
            ctx.beginPath()
            ctx.moveTo(x0, y0 + arm); ctx.lineTo(x0, y0); ctx.lineTo(x0 + arm, y0)       // TL
            ctx.moveTo(x1 - arm, y0); ctx.lineTo(x1, y0); ctx.lineTo(x1, y0 + arm)       // TR
            ctx.moveTo(x1, y1 - arm); ctx.lineTo(x1, y1); ctx.lineTo(x1 - arm, y1)       // BR
            ctx.moveTo(x0 + arm, y1); ctx.lineTo(x0, y1); ctx.lineTo(x0, y1 - arm)       // BL
            ctx.stroke()
        }

        MouseArea {
            id: fsHover
            anchors.fill: parent
            anchors.margins: -8
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: np.toggleFullscreen()
        }
    }

    // ── Hero: cover left, oversized meta right, centered as a block.
    Row {
        anchors.centerIn: parent
        width: Math.min(parent.width - 144, 980)
        spacing: 56

        Item {
            id: cover
            width: 300; height: 300
            anchors.verticalCenter: parent.verticalCenter

            // Soft drop shadow so the cover floats off the dark field. Sized
            // larger than the cover so the falloff isn't clipped to a hard box.
            ShaderEffect {
                anchors.centerIn: parent
                width: parent.width + 2 * bleed
                height: parent.height + 2 * bleed
                fragmentShader: "qrc:/shaders/covershadow.frag.qsb"
                blending: true

                // The falloff is exactly zero at `softness` past the caster's edge and
                // its support is rectangular, so covering softness plus the offset is
                // sufficient. Derived, so retuning softness cannot silently clip it.
                readonly property real bleed: softness
                    + Math.max(Math.abs(shadowOffset.x), Math.abs(shadowOffset.y))
                property color shadowColor: Qt.rgba(0, 0, 0, 0.6)
                property vector2d resolution: Qt.vector2d(width, height)
                property vector2d boxHalf: Qt.vector2d(cover.width / 2, cover.height / 2)
                property vector2d shadowOffset: Qt.vector2d(0, 24)
                property real softness: 32
            }

            Image {
                id: coverImg
                anchors.fill: parent
                source: "image://fwnp/" + np.coverId
                fillMode: Image.PreserveAspectCrop
                cache: false
            }
        }

        Column {
            width: parent.width - cover.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            MonoText {
                text: "NOW PLAYING"
                color: Theme.textAlpha(0.72)
                font.pixelSize: 10
                font.letterSpacing: 3
            }
            SansText {
                topPadding: 14
                width: parent.width
                wrapMode: Text.WordWrap
                text: np.title
                font.pixelSize: 60
                font.weight: Font.DemiBold
                font.letterSpacing: -1.5
                lineHeight: 0.98
            }
            SansText {
                topPadding: 16
                text: np.artist
                color: Theme.textAlpha(0.94)
                font.pixelSize: 24
                font.weight: Font.Medium
            }
            MonoText {
                topPadding: 12
                color: Theme.textAlpha(0.6)
                font.pixelSize: 10
                text: {
                    var parts = []
                    if (np.album) parts.push(np.album)
                    if (np.year) parts.push(np.year)
                    if (np.audioFormat) parts.push(np.audioFormat)
                    return parts.join("   ～   ")
                }
            }

            Item { width: 1; height: 30 } // spacer

            Row {
                width: Math.min(parent.width, 520)
                spacing: 14
                MonoText {
                    id: elapsed
                    anchors.verticalCenter: parent.verticalCenter
                    text: np.busyText.length > 0 ? np.busyText : fmtTime(np.position)
                    color: Theme.textAlpha(0.85)
                    font.pixelSize: 10
                }
                Rectangle {
                    id: track
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - elapsed.width - remain.width - 28
                    height: 2; radius: 1
                    color: Theme.textAlpha(0.22)
                    Rectangle {
                        width: parent.width * np.progress
                        height: parent.height
                        radius: 1
                        color: Theme.textColor
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -10 // generous hit target for the 2px bar
                        cursorShape: Qt.PointingHandCursor
                        enabled: np.duration > 0
                        onClicked: function(mouse) {
                            var r = Math.max(0, Math.min(1, mouse.x / track.width))
                            np.seek(Math.round(r * np.duration * 1000))
                        }
                    }
                }
                MonoText {
                    id: remain
                    anchors.verticalCenter: parent.verticalCenter
                    text: "-" + fmtTime(np.duration - np.position)
                    color: Theme.textAlpha(0.55)
                    font.pixelSize: 10
                }
            }
        }
    }
}
