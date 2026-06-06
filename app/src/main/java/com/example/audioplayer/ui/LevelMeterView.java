package com.example.audioplayer.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

/** L/R RMS + Peak バーをカスタム描画するレベルメータービュー */
public class LevelMeterView extends View {

    private float rmsL  = -96f, rmsR  = -96f;
    private float peakL = -96f, peakR = -96f;

    private final Paint bgPaint   = new Paint();
    private final Paint greenPaint = new Paint();
    private final Paint yellowPaint = new Paint();
    private final Paint redPaint  = new Paint();
    private final Paint peakPaint = new Paint();

    private static final float MIN_DB = -60f;
    private static final float MAX_DB =   0f;

    public LevelMeterView(Context ctx, AttributeSet attrs) {
        super(ctx, attrs);
        bgPaint.setColor(0xFF1A1A2E);
        greenPaint.setColor(0xFF4CAF50);
        yellowPaint.setColor(0xFFFFC107);
        redPaint.setColor(0xFFF44336);
        peakPaint.setColor(0xFFFFFFFF);
        peakPaint.setStrokeWidth(2f);
    }

    public void setLevels(float rmsL, float rmsR, float peakL, float peakR) {
        this.rmsL  = rmsL;
        this.rmsR  = rmsR;
        this.peakL = peakL;
        this.peakR = peakR;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        final int w = getWidth(), h = getHeight();
        final int barH = (h - 4) / 2;

        drawBar(canvas, rmsL, peakL, 0,        w, barH);
        drawBar(canvas, rmsR, peakR, barH + 4, w, barH);
    }

    private void drawBar(Canvas canvas, float rms, float peak,
                         int top, int w, int h) {
        canvas.drawRect(0, top, w, top + h, bgPaint);

        final float ratio = dbToRatio(rms);
        final int   barW  = (int)(ratio * w);

        // 緑 → 黄 → 赤のグラデーション
        final int   greenEnd  = (int)(0.75f * w);
        final int   yellowEnd = (int)(0.95f * w);

        if (barW > 0) {
            canvas.drawRect(0, top,
                Math.min(barW, greenEnd), top + h, greenPaint);
        }
        if (barW > greenEnd) {
            canvas.drawRect(greenEnd, top,
                Math.min(barW, yellowEnd), top + h, yellowPaint);
        }
        if (barW > yellowEnd) {
            canvas.drawRect(yellowEnd, top, barW, top + h, redPaint);
        }

        // ピークマーク（白い縦線）
        final int peakX = (int)(dbToRatio(peak) * w);
        if (peakX > 0 && peakX < w) {
            canvas.drawLine(peakX, top, peakX, top + h, peakPaint);
        }
    }

    /** dBFS → 0.0〜1.0 の比率に変換 */
    private float dbToRatio(float db) {
        if (db <= MIN_DB) return 0f;
        if (db >= MAX_DB) return 1f;
        return (db - MIN_DB) / (MAX_DB - MIN_DB);
    }
}
