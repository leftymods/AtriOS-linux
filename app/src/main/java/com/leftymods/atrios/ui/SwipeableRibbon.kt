package com.leftymods.atrios.ui

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import kotlinx.coroutines.launch

/**
 * Synchronous 2-card continuous ribbon:
 * As the user drags the current card horizontally, the incoming next/previous card
 * is rendered right next to it and moves in lockstep. Releasing past threshold
 * completes the slide to center and triggers onSwipeNext / onSwipePrev.
 */
@Composable
fun SwipeableTwoCardRibbon(
    trackKey: String,
    onSwipeNext: () -> Unit,
    onSwipePrev: () -> Unit,
    modifier: Modifier = Modifier,
    currentCard: @Composable () -> Unit,
    nextCard: @Composable () -> Unit,
    prevCard: @Composable () -> Unit
) {
    val scope = rememberCoroutineScope()
    val offsetX = remember { Animatable(0f) }
    var widthPx by remember { mutableIntStateOf(900) }
    var settling by remember { mutableStateOf(false) }

    // Reset offset cleanly when the new track arrives from speaker
    LaunchedEffect(trackKey) {
        offsetX.snapTo(0f)
        settling = false
    }

    val span = (widthPx + 24).toFloat().coerceAtLeast(300f)

    Box(
        modifier = modifier
            .fillMaxWidth()
            .clipToBounds()
            .onSizeChanged { if (it.width > 0) widthPx = it.width }
            .pointerInput(trackKey) {
                detectHorizontalDragGestures(
                    onDragEnd = {
                        if (settling) return@detectHorizontalDragGestures
                        val cur = offsetX.value
                        val thresh = span * 0.24f
                        scope.launch {
                            if (cur < -thresh) {
                                settling = true
                                offsetX.animateTo(-span, tween(210, easing = FastOutSlowInEasing))
                                onSwipeNext()
                                offsetX.snapTo(0f)
                                settling = false
                            } else if (cur > thresh) {
                                settling = true
                                offsetX.animateTo(span, tween(210, easing = FastOutSlowInEasing))
                                onSwipePrev()
                                offsetX.snapTo(0f)
                                settling = false
                            } else {
                                offsetX.animateTo(0f, spring(dampingRatio = 0.82f, stiffness = Spring.StiffnessMedium))
                            }
                        }
                    },
                    onDragCancel = {
                        scope.launch {
                            offsetX.animateTo(0f, spring(dampingRatio = 0.82f, stiffness = Spring.StiffnessMedium))
                        }
                    },
                    onHorizontalDrag = { _, dragAmount ->
                        if (!settling) {
                            scope.launch {
                                offsetX.snapTo((offsetX.value + dragAmount).coerceIn(-span, span))
                            }
                        }
                    }
                )
            }
    ) {
        val dx = offsetX.value
        // Incoming next card on the right when dragging left (dx < 0)
        if (dx < -1f) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .graphicsLayer { translationX = dx + span }
            ) {
                nextCard()
            }
        }
        // Incoming prev card on the left when dragging right (dx > 0)
        if (dx > 1f) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .graphicsLayer { translationX = dx - span }
            ) {
                prevCard()
            }
        }
        // Current active card moving directly under finger
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .graphicsLayer { translationX = dx }
        ) {
            currentCard()
        }
    }
}
