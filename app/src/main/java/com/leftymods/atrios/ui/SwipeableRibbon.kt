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
 * moves in lockstep. The card NEVER switches mid-drag — you can freely drag left/right
 * back and forth while holding your finger, and it ONLY commits when you release past
 * the threshold (onDragEnd).
 */
@Composable
fun SwipeableTwoCardRibbon(
    trackKey: String,
    onSwipeNext: () -> Unit,
    onSwipePrev: () -> Unit,
    modifier: Modifier = Modifier,
    canSwipeNext: Boolean = true,
    canSwipePrev: Boolean = true,
    currentCard: @Composable () -> Unit,
    nextCard: @Composable () -> Unit,
    prevCard: @Composable () -> Unit
) {
    val scope = rememberCoroutineScope()
    val offsetX = remember { Animatable(0f) }
    var widthPx by remember { mutableIntStateOf(900) }
    var settling by remember { mutableStateOf(false) }
    var isDragging by remember { mutableStateOf(false) }

    val latestNext by rememberUpdatedState(onSwipeNext)
    val latestPrev by rememberUpdatedState(onSwipePrev)
    val latestCanNext by rememberUpdatedState(canSwipeNext)
    val latestCanPrev by rememberUpdatedState(canSwipePrev)

    // Only reset offset when a new track arrives and the user is NOT actively holding/dragging
    LaunchedEffect(trackKey) {
        if (!isDragging && !settling) {
            offsetX.snapTo(0f)
        }
    }

    val span = (widthPx + 24).toFloat().coerceAtLeast(300f)

    Box(
        modifier = modifier
            .fillMaxWidth()
            .clipToBounds()
            .onSizeChanged { if (it.width > 0) widthPx = it.width }
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragStart = {
                        if (!settling) isDragging = true
                    },
                    onDragEnd = {
                        isDragging = false
                        if (settling) return@detectHorizontalDragGestures
                        val cur = offsetX.value
                        val thresh = span * 0.28f
                        scope.launch {
                            if (latestCanNext && cur < -thresh) {
                                settling = true
                                // Smooth slide out -> trigger the exact same skip action as the Next button -> snap cleanly after frame
                                offsetX.animateTo(-span, tween(190, easing = FastOutSlowInEasing))
                                latestNext()
                                withFrameNanos { }
                                offsetX.snapTo(0f)
                                settling = false
                            } else if (latestCanPrev && cur > thresh) {
                                settling = true
                                offsetX.animateTo(span, tween(190, easing = FastOutSlowInEasing))
                                latestPrev()
                                withFrameNanos { }
                                offsetX.snapTo(0f)
                                settling = false
                            } else {
                                offsetX.animateTo(0f, spring(dampingRatio = 0.82f, stiffness = Spring.StiffnessMedium))
                            }
                        }
                    },
                    onDragCancel = {
                        isDragging = false
                        if (!settling) {
                            scope.launch {
                                offsetX.animateTo(0f, spring(dampingRatio = 0.82f, stiffness = Spring.StiffnessMedium))
                            }
                        }
                    },
                    onHorizontalDrag = { change, dragAmount ->
                        if (!settling) {
                            change.consume()
                            val curSpan = (widthPx + 24).toFloat().coerceAtLeast(300f)
                            val nextPos = offsetX.value + dragAmount
                            val allowed = if (nextPos < 0f) latestCanNext else latestCanPrev
                            val delta = if (allowed) dragAmount else dragAmount * 0.20f
                            val minLim = if (latestCanNext) -curSpan else -curSpan * 0.16f
                            val maxLim = if (latestCanPrev) curSpan else curSpan * 0.16f
                            scope.launch {
                                offsetX.snapTo((offsetX.value + delta).coerceIn(minLim, maxLim))
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
