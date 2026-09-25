package com.ylports.lcsrecomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

public final class TouchControlsView extends View {
    private static final int PSP_SELECT = 0x000001;
    private static final int PSP_START = 0x000008;
    private static final int PSP_UP = 0x000010;
    private static final int PSP_RIGHT = 0x000020;
    private static final int PSP_DOWN = 0x000040;
    private static final int PSP_LEFT = 0x000080;
    private static final int PSP_L = 0x000100;
    private static final int PSP_R = 0x000200;
    private static final int PSP_TRIANGLE = 0x001000;
    private static final int PSP_CIRCLE = 0x002000;
    private static final int PSP_CROSS = 0x004000;
    private static final int PSP_SQUARE = 0x008000;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

    private int leftPointer = -1;
    private int cameraPointer = -1;
    private float analogX = 128f;
    private float analogY = 128f;
    private float cameraX = 0f;
    private float cameraY = 0f;

    public TouchControlsView(Context context) {
        super(context);
        setFocusable(true);
        fill.setColor(Color.argb(78, 255, 255, 255));
        line.setColor(Color.argb(150, 255, 255, 255));
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(3f);
        text.setColor(Color.argb(205, 255, 255, 255));
        text.setTextAlign(Paint.Align.CENTER);
        setBackgroundColor(Color.TRANSPARENT);
    }

    private float unit() {
        return Math.min(getWidth(), getHeight());
    }

    private float stickRadius() {
        return unit() * 0.115f;
    }

    private float leftCx() { return getWidth() * 0.16f; }
    private float leftCy() { return getHeight() * 0.72f; }
    private float cameraCx() { return getWidth() * 0.56f; }
    private float cameraCy() { return getHeight() * 0.76f; }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float u = unit();
        float r = stickRadius();

        drawStick(canvas, leftCx(), leftCy(), r,
                (analogX - 128f) / 127f, (analogY - 128f) / 127f, "MOVE");
        drawStick(canvas, cameraCx(), cameraCy(), r,
                cameraX / 127f, cameraY / 127f, "CAM");

        float faceR = u * 0.055f;
        float fx = getWidth() * 0.87f;
        float fy = getHeight() * 0.68f;
        drawButton(canvas, fx, fy - faceR * 1.8f, faceR, "△");
        drawButton(canvas, fx + faceR * 1.8f, fy, faceR, "○");
        drawButton(canvas, fx, fy + faceR * 1.8f, faceR, "×");
        drawButton(canvas, fx - faceR * 1.8f, fy, faceR, "□");

        float shoulderR = u * 0.048f;
        drawButton(canvas, getWidth() * 0.10f, getHeight() * 0.11f, shoulderR, "L");
        drawButton(canvas, getWidth() * 0.90f, getHeight() * 0.11f, shoulderR, "R");

        float d = u * 0.050f;
        float dx = getWidth() * 0.18f;
        float dy = getHeight() * 0.30f;
        drawButton(canvas, dx, dy - d * 1.4f, d, "↑");
        drawButton(canvas, dx + d * 1.4f, dy, d, "→");
        drawButton(canvas, dx, dy + d * 1.4f, d, "↓");
        drawButton(canvas, dx - d * 1.4f, dy, d, "←");

        float small = u * 0.037f;
        drawButton(canvas, getWidth() * 0.45f, getHeight() * 0.12f, small, "SEL");
        drawButton(canvas, getWidth() * 0.55f, getHeight() * 0.12f, small, "START");
    }

    private void drawStick(Canvas canvas, float cx, float cy, float radius,
                           float nx, float ny, String label) {
        canvas.drawCircle(cx, cy, radius, fill);
        canvas.drawCircle(cx, cy, radius, line);
        canvas.drawCircle(cx + nx * radius * 0.55f, cy + ny * radius * 0.55f,
                radius * 0.43f, fill);
        text.setTextSize(radius * 0.24f);
        canvas.drawText(label, cx, cy + radius * 1.30f, text);
    }

    private void drawButton(Canvas canvas, float cx, float cy, float radius, String label) {
        canvas.drawCircle(cx, cy, radius, fill);
        canvas.drawCircle(cx, cy, radius, line);
        text.setTextSize(Math.max(12f, radius * (label.length() > 2 ? 0.48f : 0.72f)));
        Paint.FontMetrics fm = text.getFontMetrics();
        canvas.drawText(label, cx, cy - (fm.ascent + fm.descent) * 0.5f, text);
    }

    private static boolean inCircle(float x, float y, float cx, float cy, float radius) {
        float dx = x - cx;
        float dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    }

    private int pointerIndex(MotionEvent event, int pointerId) {
        return pointerId < 0 ? -1 : event.findPointerIndex(pointerId);
    }

    private void updateStick(MotionEvent event, int pointerId, boolean camera) {
        int index = pointerIndex(event, pointerId);
        if (index < 0) {
            if (camera) {
                cameraX = cameraY = 0f;
            } else {
                analogX = analogY = 128f;
            }
            return;
        }

        float cx = camera ? cameraCx() : leftCx();
        float cy = camera ? cameraCy() : leftCy();
        float radius = stickRadius();
        float dx = event.getX(index) - cx;
        float dy = event.getY(index) - cy;
        float length = (float) Math.sqrt(dx * dx + dy * dy);
        if (length > radius && length > 0f) {
            dx = dx * radius / length;
            dy = dy * radius / length;
        }

        if (camera) {
            cameraX = dx / radius * 127f;
            cameraY = dy / radius * 127f;
        } else {
            analogX = 128f + dx / radius * 127f;
            analogY = 128f + dy / radius * 127f;
        }
    }

    private int hitButtons(MotionEvent event, int liftedIndex) {
        int buttons = 0;
        float u = unit();
        float faceR = u * 0.072f;
        float fx = getWidth() * 0.87f;
        float fy = getHeight() * 0.68f;
        float shoulderR = u * 0.065f;
        float d = u * 0.063f;
        float dx = getWidth() * 0.18f;
        float dy = getHeight() * 0.30f;
        float small = u * 0.052f;

        for (int i = 0; i < event.getPointerCount(); i++) {
            if (i == liftedIndex) continue;
            int id = event.getPointerId(i);
            if (id == leftPointer || id == cameraPointer) continue;
            float x = event.getX(i);
            float y = event.getY(i);

            if (inCircle(x, y, fx, fy - u * 0.099f, faceR)) buttons |= PSP_TRIANGLE;
            if (inCircle(x, y, fx + u * 0.099f, fy, faceR)) buttons |= PSP_CIRCLE;
            if (inCircle(x, y, fx, fy + u * 0.099f, faceR)) buttons |= PSP_CROSS;
            if (inCircle(x, y, fx - u * 0.099f, fy, faceR)) buttons |= PSP_SQUARE;

            if (inCircle(x, y, getWidth() * 0.10f, getHeight() * 0.11f, shoulderR))
                buttons |= PSP_L;
            if (inCircle(x, y, getWidth() * 0.90f, getHeight() * 0.11f, shoulderR))
                buttons |= PSP_R;

            if (inCircle(x, y, dx, dy - d * 1.4f, d)) buttons |= PSP_UP;
            if (inCircle(x, y, dx + d * 1.4f, dy, d)) buttons |= PSP_RIGHT;
            if (inCircle(x, y, dx, dy + d * 1.4f, d)) buttons |= PSP_DOWN;
            if (inCircle(x, y, dx - d * 1.4f, dy, d)) buttons |= PSP_LEFT;

            if (inCircle(x, y, getWidth() * 0.45f, getHeight() * 0.12f, small))
                buttons |= PSP_SELECT;
            if (inCircle(x, y, getWidth() * 0.55f, getHeight() * 0.12f, small))
                buttons |= PSP_START;
        }
        return buttons;
    }

    private void publish(MotionEvent event, int liftedIndex) {
        updateStick(event, leftPointer, false);
        updateStick(event, cameraPointer, true);

        int buttons = hitButtons(event, liftedIndex);
        int ax = Math.max(0, Math.min(255, Math.round(analogX)));
        int ay = Math.max(0, Math.min(255, Math.round(analogY)));
        int cx = Math.max(-127, Math.min(127, Math.round(cameraX)));
        int cy = Math.max(-127, Math.min(127, Math.round(cameraY)));
        boolean accelerate = (buttons & (PSP_CROSS | PSP_R)) != 0;
        boolean brake = (buttons & (PSP_SQUARE | PSP_L)) != 0;
        MainActivity.nativeSetInput(buttons, ax, ay, cx, cy, accelerate, brake);
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int id = event.getPointerId(actionIndex);
            float x = event.getX(actionIndex);
            float y = event.getY(actionIndex);
            float capture = stickRadius() * 1.45f;
            if (leftPointer < 0 && inCircle(x, y, leftCx(), leftCy(), capture)) {
                leftPointer = id;
            } else if (cameraPointer < 0 && inCircle(x, y, cameraCx(), cameraCy(), capture)) {
                cameraPointer = id;
            }
        }

        int lifted = -1;
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            lifted = actionIndex;
            int id = event.getPointerId(actionIndex);
            if (id == leftPointer) {
                leftPointer = -1;
                analogX = analogY = 128f;
            }
            if (id == cameraPointer) {
                cameraPointer = -1;
                cameraX = cameraY = 0f;
            }
        } else if (action == MotionEvent.ACTION_CANCEL) {
            leftPointer = cameraPointer = -1;
            analogX = analogY = 128f;
            cameraX = cameraY = 0f;
            MainActivity.nativeSetInput(0, 128, 128, 0, 0, false, false);
            invalidate();
            return true;
        }

        publish(event, lifted);
        return true;
    }
}
