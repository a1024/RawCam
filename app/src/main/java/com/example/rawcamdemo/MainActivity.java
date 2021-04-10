package com.example.rawcamdemo;

import androidx.fragment.app.FragmentActivity;

import android.os.Bundle;
import android.view.View;
import android.view.Window;

public class MainActivity extends FragmentActivity
{
	CameraFragment fragment;
	@Override protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);
		//if(savedInstanceState==null)
			getSupportFragmentManager().beginTransaction().replace(R.id.container, fragment=new CameraFragment()).commit();
	}
	@Override public void onWindowFocusChanged(boolean hasFocus)
	{
		super.onWindowFocusChanged(hasFocus);

		//set fullscreen
		Window window=getWindow();
		//window.setFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION, WindowManager.LayoutParams.TYPE_STATUS_BAR);//for transparent navigation use only this
		View decorView=window.getDecorView();
		if(hasFocus)
			decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
				|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
				|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
				|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
				|View.SYSTEM_UI_FLAG_FULLSCREEN
				|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
			);

		//if(fragment!=null)
		//	fragment.focusChanged(hasFocus);
	}
}