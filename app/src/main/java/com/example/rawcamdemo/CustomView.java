package com.example.rawcamdemo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.os.Handler;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.TextView;

import java.util.ArrayList;

public class CustomView extends View//https://stackoverflow.com/questions/31173476/android-sdk-camera2-draw-rectangle-over-textureview
{
	int w, h;
	ArrayList<String> msg=new ArrayList<>();
	boolean showTouch=false;
	float touchX, touchY;
	int textSize=35, textSize2=75;

	Context context;
	TextPaint textPaint;
	Paint paint;

	void construct(Context _context)
	{
		context=_context;
		setWillNotDraw(false);
		DisplayMetrics dm=context.getResources().getDisplayMetrics();
		w=dm.widthPixels; h=dm.heightPixels;
		//w=getWidth(); h=getHeight();//zero

		paint=new Paint(Paint.ANTI_ALIAS_FLAG);
		paint.setColor(Color.WHITE);
		paint.setStyle(Paint.Style.STROKE);
		paint.setTextSize(textSize2);

		textPaint=new TextPaint();
		textPaint.setTextSize(textSize);
		textPaint.setColor(Color.WHITE);
		setAlpha(0.99f);//turns on view transparency
	}
	public CustomView(Context _context){super(_context); construct(_context);}
	public CustomView(Context _context, AttributeSet attrs){super(_context, attrs); construct(_context);}
	public CustomView(Context _context, AttributeSet attrs, int defStyle){super(_context, attrs, defStyle); construct(_context);}

	int drawMultilineText(String str, int x, int y, int width, TextPaint paint, Canvas canvas)
	{
		int height=0;
		if(android.os.Build.VERSION.SDK_INT>=android.os.Build.VERSION_CODES.M)
		{
			StaticLayout.Builder builder=StaticLayout.Builder.obtain(str, 0, str.length(), paint, width);//android M=23
			StaticLayout sl=builder.build();
			height=sl.getHeight();
			canvas.translate(x, y);
			sl.draw(canvas);
			canvas.translate(-x, -y);
		}
		return height;
	}
	int frameCount=0;
	@Override protected void onDraw(Canvas canvas)
	{
		//super.onDraw(canvas);//does nothing
		int w2=getWidth(), h2=getHeight();
		//canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR);
		canvas.drawColor(Color.TRANSPARENT);

		//canvas.drawCircle(w>>1, h>>1, 100, paint);
		if(showTouch)
		{
			showTouch=false;
			canvas.drawCircle(touchX, touchY, 100, paint);
		}
		int count=msg.size();
		for(int k=0, y=h>>2;k<count;++k)
			y+=drawMultilineText(msg.get(k), 0, y, w, textPaint, canvas);
		//if(msgCount>0)
		//	drawMultilineText(msg, 0, h>>1, w, textPaint, canvas);
		//drawMultilineText("Hello World", 0, h>>1, 500, new TextPaint(), canvas);
		//canvas.drawText("Hello\nWorld", 0, h>>1, paint);
		canvas.drawText(""+frameCount, 0, textSize2, paint);
		++frameCount;
	}
	@Override public boolean onTouchEvent(MotionEvent event)
	{
		if(event.getAction()!=MotionEvent.ACTION_DOWN)
			return true;
		touchX=event.getX();
		touchY=event.getY();
		showTouch=true;
		invalidate();
		new Handler().postDelayed(()->
		{
			showTouch=false;
			invalidate();
		}, 4000);
		return true;
	/*	if(event.getAction()==MotionEvent.ACTION_DOWN)
		{
			invalidate();
			if(mHolder.getSurface().isValid())
			{
				final Canvas canvas=mHolder.lockCanvas();
				//Log.d("touch", "touchReceived by camera");
				if(canvas!=null)
				{
					//Log.d("touch", "touchReceived CANVAS STILL Not Null");
					canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR);
					canvas.drawColor(Color.TRANSPARENT);
					canvas.drawCircle(event.getX(), event.getY(), 100, paint);
					mHolder.unlockCanvasAndPost(canvas);
					new Handler().postDelayed(()->
					{
						Canvas canvas1 = mHolder.lockCanvas();
						if(canvas1 !=null)
						{
							canvas1.drawColor(0, PorterDuff.Mode.CLEAR);
							mHolder.unlockCanvasAndPost(canvas1);
						}
					}, 1000);
				}
				mHolder.unlockCanvasAndPost(canvas);
			}
		}
		return false;//*/
	}
	void setStatus(int duration_ms, String str)
	{
		msg.add(str);
		invalidate();
		new Handler().postDelayed(()->
		{
			if(msg.size()>0)
				msg.remove(0);
			invalidate();
		}, duration_ms);
	}
}
