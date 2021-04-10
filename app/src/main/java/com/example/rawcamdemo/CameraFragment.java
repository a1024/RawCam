package com.example.rawcamdemo;

import androidx.annotation.NonNull;
import androidx.appcompat.widget.SwitchCompat;
import androidx.core.app.ActivityCompat;
import androidx.fragment.app.Fragment;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.DngCreator;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaRecorder;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.Range;
import android.util.Rational;
import android.util.Size;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Chronometer;
import android.widget.ImageButton;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.google.android.material.slider.LabelFormatter;
import com.google.android.material.slider.Slider;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

public class CameraFragment extends Fragment//https://www.youtube.com/playlist?list=PL9jCwTXYWjDIHNEGtsRdCTk79I9-95TbJ
{
	static final boolean DEBUG=true, DEBUG_SPINNERS=false;
	static final String TAG="RawCamDemo";
	static void loge(String str){Log.e(TAG, str);}


	static final int CAMERA_REQUEST=0, STORAGE_REQUEST=1;
	static int chooseOptimalSize(Size[] choices, int width, int height)
	{
		int idx=-1;
		if(width<height)
		{
			for(int ks=0;ks<choices.length;++ks)
			{
				Size size=choices[ks];
				if(size.getWidth()==width&&size.getHeight()>=height&&(idx==-1||size.getHeight()<choices[idx].getHeight()))
					idx=ks;
			}
		}
		else
		{
			for(int ks=0;ks<choices.length;++ks)
			{
				Size size=choices[ks];
				if(size.getHeight()==height&&size.getWidth()>=width&&(idx==-1||size.getWidth()<choices[idx].getWidth()))
					idx=ks;
			}
		}
		if(idx!=-1)
			return idx;

		//no matching resolution, pick the smallest resolution larger than the screen
		for(int ks=0;ks<choices.length;++ks)
		{
			Size size=choices[ks];
			if(size.getWidth()>=width&&size.getHeight()>=height&&(idx==-1||size.getWidth()<choices[idx].getWidth()&&size.getHeight()<choices[idx].getHeight()))
				idx=ks;
		}
		if(idx!=-1)
			return idx;
		return 0;
	}


	void displayRealToast(boolean stay, String str)
	{
		Toast.makeText(getActivity(), str, stay?Toast.LENGTH_LONG:Toast.LENGTH_SHORT).show();
	}
	void displayToast(boolean stay, String str)
	{
		surfaceView.setStatus(stay?7000:4000, str);
		//Toast.makeText(getActivity(), str, stay?Toast.LENGTH_LONG:Toast.LENGTH_SHORT).show();
	}
	void error(int line, Exception e)
	{
		String msg="Error line "+line+": "+e.getMessage();//
		if(DEBUG)
			displayToast(true, msg);
		loge(msg);
		e.printStackTrace();
	}
	void error(int line, String str)
	{
		String msg="Error line "+line+": "+str;
		if(DEBUG)
			displayToast(true, msg);
		loge(msg);
	}
	int screenWidth, screenHeight;
	int textureWidth, textureHeight;
	//OrientationEventListener orientationListener;
	TextureView.SurfaceTextureListener surfaceTextureListener=new TextureView.SurfaceTextureListener()
	{
		@Override public void onSurfaceTextureAvailable(@NonNull SurfaceTexture surface, int width, int height)
		{
			//displayToast(false, "TextureView is available");
			textureWidth=width;
			textureHeight=height;
			setupCamera(false);
			connectCamera();
		}
		@Override public void onSurfaceTextureSizeChanged(@NonNull SurfaceTexture surface, int width, int height)
		{
		}
		@Override public boolean onSurfaceTextureDestroyed(@NonNull SurfaceTexture surface)
		{
			return false;
		}
		@Override public void onSurfaceTextureUpdated(@NonNull SurfaceTexture surface)
		{
			if(savePreviewRemaining>0)
			{
				savePreview();
				--savePreviewRemaining;
			}
		}
	};
	void savePreview()
	{
		if(textureView==null)
			return;
		Bitmap bitmap=textureView.getBitmap();
		bitmap=Bitmap.createBitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), textureView.getTransform(null), true );
		int bmpW=bitmap.getWidth();
		int bmpH=bitmap.getHeight();

		String filename=makeTimestamp()+".jpg";
		//String filename=makeTimestamp()+".png";
		File file=new File(folder, filename);
		try
		{
			FileOutputStream stream=new FileOutputStream(file);
			//JPEGStream stream=new JPEGStream(file);
			bitmap.compress(Bitmap.CompressFormat.JPEG, 90, stream);
			//bitmap.compress(Bitmap.CompressFormat.PNG, 90, stream);
			stream.flush();
			stream.close();
			surfaceView.setStatus(4000, "Saved "+bmpW+"x"+bmpH+" as "+filename);
		}
		catch(IOException e)
		{
			error(546, e);
		}
	}
	CameraDevice cameraDevice;
	boolean cameraOpen=false;
	HandlerThread bkHandlerThread;
	Handler bkHandler;
	Size previewSize;
	int totalRotation;
	MediaRecorder mediaRecorder;
	Chronometer chronometer;
	CaptureRequest.Builder requestBuilder;
	ImageButton videoButton, soundButton;
	boolean isRecording=false, soundOn=true;

	boolean waitingForLock=false;
	ImageReader imageReader;
	class ImageSaver implements Runnable
	{
		final Image image;
		ImageSaver(Image _image){image=_image;}
		void printSuccess(){displayToast(false, "Saved "+filename);}
		void printFailure(){displayToast(true, "Failed to save "+filename);}
		@Override public void run()
		{
			int format=image.getFormat();
			switch(format)
			{
			case ImageFormat.JPEG:
				ByteBuffer buffer=image.getPlanes()[0].getBuffer();
				byte[] bytes=new byte[buffer.remaining()];
				buffer.get(bytes);
				FileOutputStream stream=null;
				try
				{
					stream=new FileOutputStream(filename);
					stream.write(bytes);
					printSuccess();
				}
				catch(IOException e)
				{
					error(201, e);
					printFailure();
				}
				finally
				{
					image.close();
					if(stream!=null)
					{
						try
						{
							stream.close();
						}
						catch(IOException e)
						{
							error(215, e);
						}
					}
				}
				break;
			case ImageFormat.FLEX_RGB_888://TODO: save as PNG
				break;
			case ImageFormat.RAW_SENSOR:
				//if(rawSavePreview)
				//	return;
				DngCreator dngCreator=new DngCreator(characteristics, captureResult);
				FileOutputStream rawStream=null;
				try
				{
					rawStream=new FileOutputStream(filename);
					dngCreator.writeImage(rawStream, image);
					printSuccess();
				}
				catch(IOException e)
				{
					error(231, e);
					printFailure();
				}
				finally
				{
					image.close();
					if(rawStream!=null)
					{
						try
						{
							rawStream.close();
						}
						catch(IOException e)
						{
							error(245, e);
						}
					}
				}
				break;
			}
		}
	}
	CameraCaptureSession previewCaptureSession, recordCaptureSession;
	CameraCaptureSession.CaptureCallback stillCaptureCallback=new CameraCaptureSession.CaptureCallback()
	{
		@Override public void onCaptureStarted(@NonNull CameraCaptureSession session, @NonNull CaptureRequest request, long timestamp, long frameNumber)
		{
			super.onCaptureStarted(session, request, timestamp, frameNumber);
			createMediaFilename(MediaType.IMAGE);
		}
	};
	CameraCaptureSession.CaptureCallback focusCallback=new CameraCaptureSession.CaptureCallback()
	{
		@Override
		public void onCaptureCompleted(@NonNull CameraCaptureSession session, @NonNull CaptureRequest request, @NonNull TotalCaptureResult result)
		{
			super.onCaptureCompleted(session, request, result);
			captureResult=result;
			if(waitingForLock)
			{
				waitingForLock=false;
				Integer afState=result.get(CaptureResult.CONTROL_AF_STATE);
				if(afState==CaptureResult.CONTROL_AF_STATE_FOCUSED_LOCKED||afState==CaptureResult.CONTROL_AF_STATE_NOT_FOCUSED_LOCKED)
				{
					try
					{
						if(isRecording)
							requestBuilder=cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_VIDEO_SNAPSHOT);
						else
							requestBuilder=cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE);
						requestBuilder.addTarget(imageReader.getSurface());
						requestBuilder.set(CaptureRequest.JPEG_ORIENTATION, totalRotation);
						requestBuilder.set(CaptureRequest.JPEG_QUALITY, (byte)selectedQuality);
						if(manualMode)
						{
							requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_OFF);
							requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, focus);
							requestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure);

							//requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, 0.f);
							//requestBuilder.set(CaptureRequest.COLOR_CORRECTION_MODE, CameraMetadata.COLOR_CORRECTION_MODE_TRANSFORM_MATRIX);
							//requestBuilder.set(CaptureRequest.COLOR_CORRECTION_GAINS, );
							//requestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, 500000000L);//
						}
						else
						{
							if(brightnessSupported)
								requestBuilder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, brightnessBias);
							requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
						}
						if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.R)
						{
							requestBuilder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoomRatio);
						}
						if(isRecording)
							recordCaptureSession.capture(requestBuilder.build(), stillCaptureCallback, null);
						else
							previewCaptureSession.capture(requestBuilder.build(), stillCaptureCallback, null);
					}
					catch(CameraAccessException e)
					{
						error(304, e);
					}
				}
			}
		}
	};
	CaptureResult captureResult;
	CameraCharacteristics characteristics;
	ImageButton photoButton;

	static abstract class SpinnerSelectTouchListener implements AdapterView.OnItemSelectedListener, View.OnTouchListener
	{
		boolean userSelect=false;
		int lastPosition=0;
		abstract void itemSelected(AdapterView<?> parent, View view, int position, long id);
		@Override public boolean onTouch(View v, MotionEvent event)
		{
			userSelect=true;
			return false;
		}
		@Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id)
		{
			if(userSelect&&position!=lastPosition)
			{
				itemSelected(parent, view, position, id);
				lastPosition=position;
				userSelect=false;
			}
		}
		@Override public void onNothingSelected(AdapterView<?> parent){}
	}//*/
	//private void setSpinnerSelection(final Spinner spinner, final int selection)//https://stackoverflow.com/questions/2562248/how-to-keep-onitemselected-from-firing-off-on-a-newly-instantiated-spinner
	//{
	//	final AdapterView.OnItemSelectedListener listener=spinner.getOnItemSelectedListener();
	//	spinner.setOnItemSelectedListener(null);
	//	spinner.post(()->
	//	{
	//		spinner.setSelection(selection);
	//		spinner.post(()->spinner.setOnItemSelectedListener(listener));
	//	});
	//}
	TextureView textureView;
	CustomView surfaceView;
	Spinner camSpinner, imResSpinner, vidResSpinner, imFormatSpinner, jpgQualitySpinner;
	SwitchCompat uiSwitch, manualSwitch;
	Slider zoomSlider, focusSlider, exposureSlider;
	TextView labelZoom, labelFocus, labelExposure;

	boolean manualFocusEngaged=false;
/*	View.OnTouchListener textureTouchCallback=new View.OnTouchListener()
	{
		@Override public boolean onTouch(View view, MotionEvent event)//BROKEN	https://gist.github.com/royshil/8c760c2485257c85a11cafd958548482z
		{
			final int actionMasked=event.getActionMasked();
			if(actionMasked!=MotionEvent.ACTION_DOWN||manualMode||characteristics==null)
				return false;

			final Rect sensorArraySize = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);

			//TODO: here I just flip x,y, but this needs to correspond with the sensor orientation (via SENSOR_ORIENTATION)
			final int y = (int)((event.getX() / (float)view.getWidth())  * (float)sensorArraySize.height());
			final int x = (int)((event.getY() / (float)view.getHeight()) * (float)sensorArraySize.width());
			final int halfTouchWidth  = 150; //(int)event.getTouchMajor(); //TODO: this doesn't represent actual touch size in pixel. Values range in [3, 10]...
			final int halfTouchHeight = 150; //(int)event.getTouchMinor();
			MeteringRectangle focusAreaTouch = new MeteringRectangle(
				Math.max(x - halfTouchWidth,  0),
				Math.max(y - halfTouchHeight, 0),
				halfTouchWidth *2,
				halfTouchHeight*2,
				MeteringRectangle.METERING_WEIGHT_MAX-1);

			CameraCaptureSession.CaptureCallback captureCallbackHandler=new CameraCaptureSession.CaptureCallback()
			{
				@Override public void onCaptureCompleted(CameraCaptureSession session, CaptureRequest request, TotalCaptureResult result)
				{
					super.onCaptureCompleted(session, request, result);
					manualFocusEngaged = false;
					if (request.getTag() == "FOCUS_TAG") {
						//the focus trigger is complete -
						//resume repeating (preview surface will get frames), clear AF trigger
						requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER, null);
						try
						{
							previewCaptureSession.setRepeatingRequest(requestBuilder.build(), null, null);
						}
						catch(CameraAccessException e)
						{
							e.printStackTrace();
						}
					}
				}
				@Override public void onCaptureFailed(CameraCaptureSession session, CaptureRequest request, CaptureFailure failure)
				{
					super.onCaptureFailed(session, request, failure);
					error(357, "Manual AF failure: " + failure);
					//Log.e(TAG, "Manual AF failure: " + failure);
					manualFocusEngaged=false;
				}
			};

			//first stop the existing repeating request
			try
			{
				previewCaptureSession.stopRepeating();

				//cancel any existing AF trigger (repeated touches, etc.)
				requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER, CameraMetadata.CONTROL_AF_TRIGGER_CANCEL);
				requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_OFF);
				previewCaptureSession.capture(requestBuilder.build(), captureCallbackHandler, bkHandler);

				//Now add a new AF trigger with focus region
				if (characteristics.get(CameraCharacteristics.CONTROL_MAX_REGIONS_AF)>=1)
					requestBuilder.set(CaptureRequest.CONTROL_AF_REGIONS, new MeteringRectangle[]{focusAreaTouch});
				requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
				requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO);
				requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER, CameraMetadata.CONTROL_AF_TRIGGER_START);
				requestBuilder.setTag("FOCUS_TAG"); //we'll capture this later for resuming the preview

				//then we ask for a single request (not repeating!)
				previewCaptureSession.capture(requestBuilder.build(), captureCallbackHandler, bkHandler);
			}
			catch(CameraAccessException e)
			{
				e.printStackTrace();
			}

			manualFocusEngaged=true;
			return true;
		}
	};//*/
/*	static class JPEGStream extends FileOutputStream//does nothing		https://superuser.com/questions/1509194/windows-photo-viewer-cant-run-because-not-enough-memory
	{
		boolean marker=false, skipSegment=false;
		public JPEGStream(String name)throws FileNotFoundException{super(name);}
		public JPEGStream(File file)throws FileNotFoundException{super(file);}
		@Override public void write(int b)throws IOException
		{
			//Based on https://en.wikipedia.org/wiki/JPEG#Syntax_and_structure
			if(this.marker)
			{
				this.marker=false;
				if((b&0xFF)==0xE2)//The 0xFF,0xE2 segment that Android writes seems to cause trouble with Windows Photo Viewer.
					this.skipSegment=true;
				else
				{
					this.skipSegment=false;
					super.write(0xFF);
					super.write(b);
				}
			}
			else if((b&0xFF)==0xFF)
				this.marker = true;
			else if(!this.skipSegment)
				super.write(b);
		}
	}//*/
	//SurfaceHolder surfaceHolder;
	//@Override public void onCreate(Bundle savedInstanceState)
	@Override public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState)
	{
		return inflater.inflate(R.layout.fragment_main, container, false);
	}
	@Override public void onViewCreated(final View view, Bundle savedInstanceState)
	{
		//super.onCreate(savedInstanceState);
		//setContentView(R.layout.fragment_main);

		surfaceView=view.findViewById(R.id.surfaceView);
	//	surfaceHolder=surfaceView.getHolder();
	/*	surfaceHolder.addCallback(new SurfaceHolder.Callback()
		{
			@Override public void surfaceCreated(@NonNull SurfaceHolder holder)
			{
				Canvas canvas=holder.lockCanvas();
				if (canvas==null)
					Log.e(TAG, "Cannot draw onto the canvas as it's null");
				else
				{
					Paint myPaint=new Paint();
					myPaint.setColor(Color.rgb(100, 20, 50));
					myPaint.setStrokeWidth(10);
					myPaint.setStyle(Paint.Style.STROKE);
					canvas.drawRect(100, 100, 200, 200, myPaint);

					holder.unlockCanvasAndPost(canvas);
				}
			}
			@Override public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height){}
			@Override public void surfaceDestroyed(@NonNull SurfaceHolder holder){}
		});//*/

		//createFolder();
		File movieFile=Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM);
		folder=new File(movieFile, "Raw");
		if(!folder.exists())
		{
			boolean success=folder.mkdirs();
			if(!success)
				error(400, "Failed to create folder \'DCIM/Raw\'");
		}

		DisplayMetrics displayMetrics = new DisplayMetrics();
		getActivity().getWindowManager().getDefaultDisplay().getMetrics(displayMetrics);
		screenWidth=displayMetrics.widthPixels;
		screenHeight=displayMetrics.heightPixels;

		mediaRecorder=new MediaRecorder();
		textureView=view.findViewById(R.id.textureView);
		//int screenRotation=getWindowManager().getDefaultDisplay().getRotation();
		//float surfaceRotation=0;
		//switch(screenRotation)
		//{
		//case Surface.ROTATION_0://0
		//	break;
		//case Surface.ROTATION_90://1
		//	surfaceRotation=90;
		//	break;
		//case Surface.ROTATION_180://2
		//	surfaceRotation=180;
		//	break;
		//case Surface.ROTATION_270://3
		//	surfaceRotation=270;
		//	break;
		//}
		//textureView.setRotation(surfaceRotation);//CROPPED

		//int orientation=getResources().getConfiguration().orientation;
		//switch(orientation)
		//{
		//case Configuration.ORIENTATION_LANDSCAPE:
		//}
		//textureView.setOnTouchListener(textureTouchCallback);

		videoButton=view.findViewById(R.id.videoButton);
		videoButton.setImageResource(R.mipmap.btn_video_foreground);
		videoButton.setOnClickListener(v->
		{
			if(isRecording)//stop recording
			{
				chronometer.stop();
				chronometer.setVisibility(View.INVISIBLE);
				videoButton.setImageResource(R.mipmap.btn_video_foreground);
				mediaRecorder.stop();
				mediaRecorder.reset();
				startPreview();
				soundButton.setVisibility(View.VISIBLE);
			}
			else//start recording
			{
				if(rawSavePreview)
				{
					surfaceView.setStatus(4000, "Burst of 20 begins in 4 seconds...");
					new Handler().postDelayed(()->savePreviewRemaining=20, 4000);
					return;
				}
				if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.M&&ActivityCompat.checkSelfPermission(getActivity(), Manifest.permission.WRITE_EXTERNAL_STORAGE)!=PackageManager.PERMISSION_GRANTED)
				{
					if(shouldShowRequestPermissionRationale(Manifest.permission.WRITE_EXTERNAL_STORAGE))
						displayRealToast(true, "Need permission for external storage to save media");
					requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, STORAGE_REQUEST);
					return;
				}
				videoButton.setImageResource(R.mipmap.btn_recording_foreground);
				startChronometer();
				soundButton.setVisibility(View.INVISIBLE);
				createMediaFilename(MediaType.VIDEO);
				startRecord();
				mediaRecorder.start();
			}
			isRecording=!isRecording;
		});
		soundButton=view.findViewById(R.id.soundButton);
		soundButton.setImageResource(R.mipmap.btn_sound_on_foreground);
		soundButton.setOnClickListener(v->
		{
			if(soundOn)//switch off sound
				soundButton.setImageResource(R.mipmap.btn_sound_off_foreground);
			else//switch on sound
				soundButton.setImageResource(R.mipmap.btn_sound_on_foreground);
			soundOn=!soundOn;
		});
		chronometer=view.findViewById(R.id.counter);

		photoButton=view.findViewById(R.id.photoButton);
		photoButton.setImageResource(R.mipmap.btn_photo_foreground);
		photoButton.setOnClickListener(v->
		{
			if(rawSavePreview)
			{
				savePreviewRemaining=1;
				//savePreview();
				return;
			}
			waitingForLock=true;
			requestBuilder.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_START);
			try
			{
				if(isRecording)//capture image while recording
					recordCaptureSession.capture(requestBuilder.build(), focusCallback, bkHandler);
				else//normal image capture
					previewCaptureSession.capture(requestBuilder.build(), focusCallback, bkHandler);
			}
			catch(CameraAccessException e)
			{
				error(491, e);
			}
		});

		camSpinner=view.findViewById(R.id.camSpinner);
		imResSpinner=view.findViewById(R.id.imResSpinner);
		vidResSpinner=view.findViewById(R.id.vidResSpinner);
		imFormatSpinner=view.findViewById(R.id.imFormatSpinner);
		jpgQualitySpinner=view.findViewById(R.id.jpgQualitySpinner);
		SpinnerSelectTouchListener camListener=new SpinnerSelectTouchListener()//select camera
		{
			@Override void itemSelected(AdapterView<?> parent, View view, int position, long id)//TODO: lazy if(selectedCam!=activeCam)...
			{
				if(DEBUG_SPINNERS)
				{
					//surfaceView.setStatus(1000, "cam "+instance+": "+position);
					surfaceView.setStatus(1000, "cam: "+position);
					//surfaceView.setStatus(1000, "Did you change camera ID to #"+position+"?");
					return;
				}
				if(!camInfo_uninit)
				{
					selectedCam=position;
					//TODO: populate format & resolution spinners

					if(textureView.isAvailable())
					{
						closeCamera();

						setupCamera(true);
						connectCamera();
					}
				}
			}
			//@Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id){}
			//@Override public void onNothingSelected(AdapterView<?> parent){}
		};
		camSpinner.post(()->
		{
			camSpinner.setOnItemSelectedListener(camListener);
			camSpinner.setOnTouchListener(camListener);
		});

		ArrayAdapter<String> adapter=new ArrayAdapter<>(getActivity(), R.layout.spinner_item);
		adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		for(int k=0;k<=100;k+=5)
			adapter.add(String.format(Locale.US, "%d", k));
		jpgQualitySpinner.setAdapter(adapter);
		selectedQuality=75;
		jpgQualitySpinner.setSelection(selectedQuality/5, false);

		SpinnerSelectTouchListener imFormatListener=new SpinnerSelectTouchListener()//image format
		{
			//int skip=2000000;
			@Override void itemSelected(AdapterView<?> parent, View view, int position, long id)
			{
				if(DEBUG_SPINNERS)
				{
					//surfaceView.setStatus(1000, "imFormat "+instance+": "+position);
					surfaceView.setStatus(1000, "imFormat: "+position);
					//surfaceView.setStatus(1000, "Did you change image format to #"+position+"?");
					return;
				}
				//if(skip>0)
				//{
				//	--skip;
				//	return;
				//}
				switch(position)
				{
				case 0://jpeg
					selectedImFormat=ImageFormat.JPEG;
					manualSwitch.setVisibility(View.VISIBLE);//
					break;
				case 1://loss-less (saved as PNG)
					if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.M)
						selectedImFormat=ImageFormat.FLEX_RGB_888;
					manualSwitch.setVisibility(View.VISIBLE);//
					break;
				case 2://raw
				case 3://raw preview		//raw+jpeg
					selectedImFormat=ImageFormat.RAW_SENSOR;
					manualSwitch.setVisibility(View.INVISIBLE);//won't capture raw in manual mode
					break;
				}
				rawSavePreview=position==3;
				//rawPlusJPEG=position==3;
				if(selectedImFormat==ImageFormat.JPEG)
					jpgQualitySpinner.setVisibility(View.VISIBLE);
				else
					jpgQualitySpinner.setVisibility(View.INVISIBLE);
				populateImResSpinner();

				changeImRes();

				//if(textureView.isAvailable())
				//{
				//	setImRes();
				//	connectCamera();
				//}
				//else
				//	textureView.setSurfaceTextureListener(surfaceTextureListener);

				//if(textureView.isAvailable())
				//	setImRes();
			}
			//@Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id){}
			//@Override public void onNothingSelected(AdapterView<?> parent){}
		};
		imFormatSpinner.post(()->
		{
			imFormatSpinner.setOnItemSelectedListener(imFormatListener);
			imFormatSpinner.setOnTouchListener(imFormatListener);
		});

		SpinnerSelectTouchListener imResListener=new SpinnerSelectTouchListener()//image resolution
		{
			//int skip=2000000;
			@Override void itemSelected(AdapterView<?> parent, View view, int position, long id)
			{
				if(DEBUG_SPINNERS)
				{
					//surfaceView.setStatus(1000, "imRes "+instance+": "+position);
					surfaceView.setStatus(1000, "imRes: "+position);
					//surfaceView.setStatus(1000, "Did you change image resolution to #"+position+"?");
					return;
				}
				//if(skip>0)
				//{
				//	--skip;
				//	return;
				//}
				selectedImRes=position;

				changeImRes();

				//if(textureView.isAvailable())//works but with exceptions: already open
				//{
				//	setImRes();
				//	connectCamera();
				//}
				//else
				//	textureView.setSurfaceTextureListener(surfaceTextureListener);

				//if(textureView.isAvailable())//CRASH: CaptureRequest contains unconfigured Input/Output Surface
				//	setImRes();
			}
			//@Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id){}
			//@Override public void onNothingSelected(AdapterView<?> parent){}
		};
		imResSpinner.post(()->
		{
			imResSpinner.setOnItemSelectedListener(imResListener);
			imResSpinner.setOnTouchListener(imResListener);
		});

		SpinnerSelectTouchListener jpgQualityListener=new SpinnerSelectTouchListener()//jpeg quality
		{
			@Override void itemSelected(AdapterView<?> parent, View view, int position, long id)
			{
				if(DEBUG_SPINNERS)
				{
					//surfaceView.setStatus(1000, "jpgQuality "+instance+": "+position);
					surfaceView.setStatus(1000, "jpgQuality: "+position);
					//surfaceView.setStatus(1000, "Did you change JPEG quality to #"+position+"?");
					return;
				}
				selectedQuality=position*5;
			}
			//@Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id){}
			//@Override public void onNothingSelected(AdapterView<?> parent){}
		};
		jpgQualitySpinner.post(()->
		{
			jpgQualitySpinner.setOnItemSelectedListener(jpgQualityListener);
			jpgQualitySpinner.setOnTouchListener(jpgQualityListener);
		});

		SpinnerSelectTouchListener vidResListener=new SpinnerSelectTouchListener()//video resolution
		{
			@Override void itemSelected(AdapterView<?> parent, View view, int position, long id)
			{
				if(DEBUG_SPINNERS)
				{
					//surfaceView.setStatus(1000, "vidRes "+instance+": "+position);
					surfaceView.setStatus(1000, "vidRes: "+position);
					//surfaceView.setStatus(1000, "Did you change video resolution to #"+position+"?");
					return;
				}
				selectedVidRes=position;
			}
			//@Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id){}
			//@Override public void onNothingSelected(AdapterView<?> parent){}
		};
		vidResSpinner.post(()->
		{
			vidResSpinner.setOnItemSelectedListener(vidResListener);
			vidResSpinner.setOnTouchListener(vidResListener);
		});

		uiSwitch=view.findViewById(R.id.uiSwitch);
		manualSwitch=view.findViewById(R.id.manualSwitch);
		zoomSlider=view.findViewById(R.id.zoomSlider);
		focusSlider=view.findViewById(R.id.focusSlider);
		exposureSlider=view.findViewById(R.id.exposureSlider);
		labelZoom=view.findViewById(R.id.labelZoom);
		labelFocus=view.findViewById(R.id.labelFocus);
		labelExposure=view.findViewById(R.id.labelExposure);
		manualSwitch.setChecked(manualMode);
		float uiAlpha=0.8f;
		uiSwitch.setAlpha(uiAlpha);
		manualSwitch.setAlpha(uiAlpha);
		zoomSlider.setAlpha(uiAlpha);
		focusSlider.setAlpha(uiAlpha);
		exposureSlider.setAlpha(uiAlpha);
		photoButton.setAlpha(uiAlpha);
		videoButton.setAlpha(uiAlpha);
		soundButton.setAlpha(uiAlpha);
		hideManualUI();
		uiSwitch.setChecked(true);
		uiSwitch.setOnClickListener(v->
		{
			showUI=!showUI;
			int vis=showUI?View.VISIBLE:View.INVISIBLE;
			soundButton.setVisibility(vis);
			camSpinner.setVisibility(vis);
			imResSpinner.setVisibility(vis);
			vidResSpinner.setVisibility(vis);
			imFormatSpinner.setVisibility(vis);
			jpgQualitySpinner.setVisibility(vis);
			manualSwitch.setVisibility(vis);
			zoomSlider.setVisibility(vis);
			labelZoom.setVisibility(vis);
			if(manualMode)
			{
				focusSlider.setVisibility(vis);
				labelFocus.setVisibility(vis);
				//balanceSlider.setVisibility(vis);
				//labelBalance.setVisibility(vis);
				exposureSlider.setVisibility(vis);
				labelExposure.setVisibility(vis);
			}
		});
		zoomSlider.addOnChangeListener((slider, value, fromUser)->
		{
			zoomRatio=value;
			setLabelZoom();
			setSettings();
		});
		manualSwitch.setOnClickListener(v->
		{
			manualMode=!manualMode;
			if(manualMode)
			{
				CameraManager manager=(CameraManager)getActivity().getSystemService(Context.CAMERA_SERVICE);
				String camId=getCameraId();
				boolean focusSupported=false, exposureSupported=false;
				try
				{
					CameraCharacteristics characteristics=manager.getCameraCharacteristics(camId);

					focusMin=characteristics.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE);
					if(focusMin>0)
					{
						focusSupported=true;
						focusSlider.setValueFrom(0);
						focusSlider.setValueTo(focusMin);
						focusSlider.setValue(focus=focusMin/2);
					}

					Range<Long> exposureRange=characteristics.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE);
					exposureMin=exposureRange.getLower();//ns
					exposureMax=exposureRange.getUpper();
					if(exposureMin<exposureMax)
					{
						exposureSupported=true;
						float exStart=(float)Math.log(exposureMin*1e-6), exEnd=(float)Math.log(exposureMax*1e-6),//log(ms)
							exDefault=(float)Math.log(20);//20ms
						exposureSlider.setValueFrom(exStart);
						exposureSlider.setValueTo(exEnd);
						exposureSlider.setValue(exDefault);
						exposureSlider.setStepSize(0);
						exposure=(long)(Math.exp(exDefault)*1e6);
					}

					//requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_OFF);
					if(focusSupported)
						setLabelFocus();
					if(exposureSupported)
						setLabelBrightnessExposure();
					if(focusSupported||exposureSupported)
						setSettings();
				}
				catch(CameraAccessException e)
				{
					error(749, e);
				}
				if(focusSupported)
				{
					focusSlider.setVisibility(View.VISIBLE);
					labelFocus.setVisibility(View.VISIBLE);
				}
				if(exposureSupported)
				{
					exposureSlider.setVisibility(View.VISIBLE);
					labelExposure.setVisibility(View.VISIBLE);
				}
			}
			else//switch to auto
			{
				if(brightnessSupported)
					setSliderBrightness();
				else
				{
					exposureSlider.setVisibility(View.INVISIBLE);
					labelExposure.setVisibility(View.INVISIBLE);
				}
				hideManualUI();
				releaseSettings();
			}
		});
		focusSlider.addOnChangeListener((slider, value, fromUser)->
		{
			focus=value;
			setLabelFocus();
			setSettings();
		});
		exposureSlider.addOnChangeListener((slider, value, fromUser)->
		{
			if(manualMode)
				exposure=(long)(Math.exp(value)*1e6);
			else
				brightnessBias=(int)value;
			setLabelBrightnessExposure();
			setSettings();
		});
		exposureSlider.setLabelFormatter(value->
		{
			if(manualMode)
				return String.format(Locale.US, "%.3fms", Math.exp(value));
			return String.format(Locale.US, "%.2f EV", value*brightnessStep);
		});

		//orientationListener=new OrientationEventListener(getActivity(), SensorManager.SENSOR_DELAY_NORMAL)
		//{
		//	int oldOrientation;
		//	@Override public void onOrientationChanged(int orientation)
		//	{
		//		if(textureView!=null&&textureView.isAvailable()&&orientation/90!=oldOrientation/90)
		//		{
		//			setupCamera(true);
		//			oldOrientation=orientation;
		//		}
		//	}
		//};
	}

	@Override public void onResume()
	{
		super.onResume();
		startBkThread();

		if(textureView.isAvailable())
		{
			textureWidth=textureView.getWidth();//orientation may have changed
			textureHeight=textureView.getHeight();
			setupCamera(true);
			connectCamera();
		}
		else
			textureView.setSurfaceTextureListener(surfaceTextureListener);
		//if(orientationListener!=null&&orientationListener.canDetectOrientation())
		//	orientationListener.enable();
	}
	@Override public void onPause()
	{
		//if(orientationListener!=null)
		//	orientationListener.disable();
		closeCamera();

		stopBkThread();
		super.onPause();
	}

	boolean showUI=true, manualMode=false,
		supportsManual=false, supportsReadSettings=false;
	float focus, focusMin;//[0, focusMin]
	boolean focusDiopter=false;
	long exposure, exposureMin, exposureMax;
	boolean zoomSupported=false;
	float zoomRatio=1, zoomMin, zoomMax;
	boolean brightnessSupported=false;
	int brightnessBias, brightnessMin, brightnessMax;
	float brightnessStep;
	void setLabelFocus()
	{
		if(focusDiopter)
		{
			if(focus==0)
				labelFocus.setText(String.format(Locale.US, "Focus [Inf-%.2f]: Inf", 1/focusMin));
			else
				labelFocus.setText(String.format(Locale.US, "Focus [Inf-%.2f]: %.2fm", 1/focusMin, 1/focus));
		}
		else
			labelFocus.setText(String.format(Locale.US, "Focus [0-%.2f]: %.2f", focusMin, focus));
	}
	void setLabelBrightnessExposure()
	{
		if(manualMode)
			labelExposure.setText(String.format(Locale.US, "Exposure [%.3f-%.2f]: %.2fms", exposureMin*1e-6, exposureMax*1e-6, exposure*1e-6));
		else
			labelExposure.setText(String.format(Locale.US, "Brightness [%.2f-%.2f]: %.2f EV", brightnessMin*brightnessStep, brightnessMax*brightnessStep, brightnessBias*brightnessStep));
	}
	void setSliderBrightness()
	{
		exposureSlider.setValueTo(brightnessMax);
		exposureSlider.setValueFrom(brightnessMin);
		exposureSlider.setValue(brightnessBias);
		exposureSlider.setStepSize(1);
	}
	void setLabelZoom(){labelZoom.setText(String.format(Locale.US, "Zoom [%.2f-%.2f]: %.2f", zoomMin, zoomMax, zoomRatio));}
	void setSettings()
	{
		CameraCaptureSession session;
		if(isRecording)
			session=recordCaptureSession;
		else
			session=previewCaptureSession;
		if(session==null)
			return;
		try
		{
			session.stopRepeating();
			if(manualMode)
			{
				requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_OFF);
				requestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure);
				requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_OFF);
				requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, focus);
			}
			else if(brightnessSupported)
				requestBuilder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, brightnessBias);
			if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.R)
			{
				requestBuilder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoomRatio);
			}
			session.setRepeatingRequest(requestBuilder.build(), null, null);
		}
		catch(CameraAccessException e)
		{
			error(850, e);
		}
	}
	void releaseSettings()
	{
		CameraCaptureSession session;
		if(isRecording)
			session=recordCaptureSession;
		else
			session=previewCaptureSession;
		if(session==null)
			return;
		try
		{
			session.stopRepeating();
			requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
			requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO);
			session.setRepeatingRequest(requestBuilder.build(), null, null);
		}
		catch(CameraAccessException e)
		{
			error(864, e);
		}
	}
	void hideManualUI()
	{
		//balanceSlider.setVisibility(View.INVISIBLE);
		//labelBalance.setVisibility(View.INVISIBLE);
		focusSlider.setVisibility(View.INVISIBLE);
		labelFocus.setVisibility(View.INVISIBLE);
		//exposureSlider.setVisibility(View.INVISIBLE);
		//labelExposure.setVisibility(View.INVISIBLE);
	}
	boolean supportsRaw=false;
	boolean camInfo_uninit=true;
	ArrayList<String> camIds=new ArrayList<>();
	ArrayList<Size> imResList=new ArrayList<>(), vidResList=new ArrayList<>();
	int selectedCam=0, selectedImRes=0, selectedVidRes=0, selectedImFormat=ImageFormat.JPEG, selectedQuality=75;
	boolean rawSavePreview=false;
	int savePreviewRemaining=0;
	//boolean rawPlusJPEG=false;
	int clampIdx(int idx, int size)
	{
		if(idx>=size)
			idx=size-1;
		if(idx<0)
			idx=0;
		return idx;
	}
	String getCameraId()
	{
		int count=camIds.size();
		if(count==0)
		{
			error(894, "camIds is empty");
			return "0";
		}
		selectedCam=clampIdx(selectedCam, count);
		return camIds.get(selectedCam);
	}
	Size getImRes()
	{
		int count=imResList.size();
		if(count==0)
			error(904, "imResList is empty");
		selectedImRes=clampIdx(selectedImRes, count);
		return imResList.get(selectedImRes);
	}
	void setImRes()//uses selectedImRes & selectedImFormat
	{
		Size imageSize=getImRes();
		imageReader=ImageReader.newInstance(imageSize.getWidth(), imageSize.getHeight(), selectedImFormat, 1);
		imageReader.setOnImageAvailableListener(reader->bkHandler.post(new ImageSaver(reader.acquireLatestImage())), bkHandler);
	}
	void changeImRes()
	{
	/*	Size imageSize=getImRes();//WIP
		imageReader=ImageReader.newInstance(imageSize.getWidth(), imageSize.getHeight(), selectedImFormat, 1);
		imageReader.setOnImageAvailableListener(reader->bkHandler.post(new ImageSaver(reader.acquireLatestImage())), bkHandler);
		Surface previewSurface=new Surface(texture);
		try
		{
			cameraDevice.createCaptureSession(Arrays.asList(previewSurface, imageReader.getSurface()), new CameraCaptureSession.StateCallback()
			{
				@Override public void onConfigured(@NonNull CameraCaptureSession session)
				{
					previewCaptureSession=session;
					try
					{
						previewCaptureSession.setRepeatingRequest(requestBuilder.build(), null, bkHandler);
					}
					catch(CameraAccessException e)
					{
						error(953, e);
					}
				}
				@Override public void onConfigureFailed(@NonNull CameraCaptureSession session)
				{
					error(958, "Unable to setup camera preview");
					//displayToast(false, "Unable to setup camera preview");
				}
			}, null);
		}
		catch(CameraAccessException e)
		{
			error(1022, e);
		}//*/


		Size imageSize=getImRes();//inline startPreview()	CRASH when called twice
		imageReader=ImageReader.newInstance(imageSize.getWidth(), imageSize.getHeight(), selectedImFormat, 1);
		imageReader.setOnImageAvailableListener(reader->bkHandler.post(new ImageSaver(reader.acquireLatestImage())), bkHandler);
		SurfaceTexture texture=textureView.getSurfaceTexture();
		if(DEBUG)
			displayToast(false, "Preview resolution: "+previewSize.getWidth()+"x"+previewSize.getHeight());//
		texture.setDefaultBufferSize(previewSize.getWidth(), previewSize.getHeight());
		Surface previewSurface=new Surface(texture);
		try
		{
			requestBuilder=cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
			if(manualMode)
			{
				requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_OFF);
				requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, focus);
				requestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure);
			}
			else
			{
				requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
				if(brightnessSupported)
					requestBuilder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, brightnessBias);
			}
			if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.R)
			{
				requestBuilder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoomRatio);
			}
			requestBuilder.addTarget(previewSurface);
			cameraDevice.createCaptureSession(Arrays.asList(previewSurface, imageReader.getSurface()), new CameraCaptureSession.StateCallback()
			{
				@Override public void onConfigured(@NonNull CameraCaptureSession session)
				{
					previewCaptureSession=session;
					try
					{
						previewCaptureSession.setRepeatingRequest(requestBuilder.build(), null, bkHandler);
					}
					catch(CameraAccessException e)
					{
						error(953, e);
					}
				}
				@Override public void onConfigureFailed(@NonNull CameraCaptureSession session)
				{
					error(958, "Unable to setup camera preview");
					//displayToast(false, "Unable to setup camera preview");
				}
			}, null);
		}
		catch(CameraAccessException e)
		{
			error(965, e);
		}
	}
	Size getVidRes()
	{
		int count=vidResList.size();
		if(count==0)
			error(1051, "vidResList is empty");
		selectedVidRes=clampIdx(selectedVidRes, count);
		return vidResList.get(selectedVidRes);
	}
	void populateStrSpinner(Spinner s, String[] choices, int position)
	{
		ArrayAdapter<String> adapter=new ArrayAdapter<>(getActivity(), R.layout.spinner_item);
		//ArrayAdapter<String> adapter=new ArrayAdapter<>(this, android.R.layout.simple_spinner_item);
		adapter.addAll(choices);
		adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		s.setAdapter(adapter);
		s.setSelection(position, false);
	}
	void populateResSpinner(Spinner s, Size[] resolutions, int position)
	{
		ArrayAdapter<String> adapter=new ArrayAdapter<>(getActivity(), R.layout.spinner_item);
		//ArrayAdapter<String> adapter=new ArrayAdapter<>(this, android.R.layout.simple_spinner_item);
		for(Size res:resolutions)
		{
			String str=String.format(Locale.US, "%dx%d %.2fMP", res.getWidth(), res.getHeight(), res.getWidth()*res.getHeight()*(1.f/(1024*1024)));
			adapter.add(str);
		}
		adapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
		//adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		s.setAdapter(adapter);
		s.setSelection(position, false);
	}
	void populateImResSpinner()
	{
		CameraManager manager=(CameraManager)getActivity().getSystemService(Context.CAMERA_SERVICE);
		String id=getCameraId();
		try
		{
			CameraCharacteristics characteristics=manager.getCameraCharacteristics(id);
			StreamConfigurationMap map=characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
			Size[] resolutions=map.getOutputSizes(selectedImFormat);//image capture resolutions

			imResList.clear();
			imResList.addAll(Arrays.asList(resolutions));

			selectedImRes=0;//TODO: select largest resolution by default
			populateResSpinner(imResSpinner, resolutions, selectedImRes);
		}
		catch(CameraAccessException e)
		{
			error(1096, e);
		}
	}
	void rotatePreview(int width, int height, int angle)
	{
		float cx=textureView.getWidth()*0.5f, cy=textureView.getHeight()*0.5f;
		float h_w=(float)height/width, w_h=(float)width/height;
		Matrix matrix=new Matrix();
		matrix.postScale(h_w, w_h, cx, cy);
		matrix.postRotate(angle, cx, cy);
		textureView.setTransform(matrix);
	}
	void setupCamera(boolean userSelectedCam)
	{
		CameraManager manager=(CameraManager)getActivity().getSystemService(Context.CAMERA_SERVICE);
		try
		{
			if(!userSelectedCam)
			{
				String[] camIds=manager.getCameraIdList();

				if(camInfo_uninit)//populate camSpinner
				{
					this.camIds.ensureCapacity(camIds.length);
					ArrayAdapter<String> adapter=new ArrayAdapter<>(getActivity(), R.layout.spinner_item);//TODO: populate string spinner
					//ArrayAdapter<String> adapter=new ArrayAdapter<>(this, android.R.layout.simple_spinner_item);
					for(String id:camIds)
					{
						//CameraCharacteristics characteristics=manager.getCameraCharacteristics(id);
						//StreamConfigurationMap map=characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
						this.camIds.add(id);
						adapter.add(id);
					}
					adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
					camSpinner.setAdapter(adapter);
					camInfo_uninit=false;
				}

				for(selectedCam=0;selectedCam<camIds.length;++selectedCam)
				{
					characteristics=manager.getCameraCharacteristics(camIds[selectedCam]);
					if(characteristics.get(CameraCharacteristics.LENS_FACING)!=CameraCharacteristics.LENS_FACING_FRONT)
						break;
				}
			}

			//check camera2 support
			//int hwLevel=characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL);

			//initialize zoom
			if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
			{
				Range<Float> zoomRange=characteristics.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE);
				if(zoomRange!=null)
				{
					zoomSupported=true;
					zoomMin=zoomRange.getLower();
					zoomMax=zoomRange.getUpper();
					zoomSlider.setValueFrom(zoomMin);
					zoomSlider.setValueTo(zoomMax);
					zoomRatio=1;
					zoomSlider.setValue(zoomRatio);
				}
			}
			if(!zoomSupported)
			{
				zoomSlider.setVisibility(View.INVISIBLE);
				labelZoom.setVisibility(View.INVISIBLE);
			}

			//initialize brightness
			Range<Integer> brightnessRange=characteristics.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE);
			Rational brightnessStepFraction=characteristics.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP);
			if(brightnessSupported=brightnessRange!=null&&brightnessStepFraction!=null)
			{
				brightnessMin=brightnessRange.getLower();
				brightnessMax=brightnessRange.getUpper();
				brightnessStep=brightnessStepFraction.floatValue();
				//if(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION==null)
					brightnessBias=(brightnessMin+brightnessMax)>>1;
				//else
				//	brightnessBias=characteristics.get(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION);
				setSliderBrightness();
				setLabelBrightnessExposure();
				exposureSlider.setVisibility(View.VISIBLE);
				labelExposure.setVisibility(View.VISIBLE);
			}
			else
			{
				exposureSlider.setVisibility(View.INVISIBLE);
				labelExposure.setVisibility(View.INVISIBLE);
			}

			//check raw support
			int[] capabilities=characteristics.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
			for(int c: capabilities)
			{
				switch(c)
				{
				case CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_RAW:
					supportsRaw=true;
					break;
				case CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR:
					supportsManual=true;
					break;
				case CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_READ_SENSOR_SETTINGS:
					supportsReadSettings=true;
					break;
				}
			}
			if(!supportsManual)
				manualSwitch.setVisibility(View.INVISIBLE);

			int focusType=characteristics.get(CameraCharacteristics.LENS_INFO_FOCUS_DISTANCE_CALIBRATION);
			if(focusType!=CameraMetadata.LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED)
			{
				focusDiopter=true;
				focusSlider.setLabelFormatter(value->
				{
					if(value==0)
						return "Inf";
					return String.format(Locale.US, "%.2fm", 1/value);
				});
			}

			populateImResSpinner();
			setImRes();
			Size imRes=getImRes();

			StreamConfigurationMap map=characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
			assert map!=null;
			int deviceOrientation=getActivity().getWindowManager().getDefaultDisplay().getRotation();

			int sensorOrientation=characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);//90	means portrait in natural position
			switch(deviceOrientation)
			{
			case Surface.ROTATION_0:	deviceOrientation=0;break;
			case Surface.ROTATION_90:	deviceOrientation=90;break;
			case Surface.ROTATION_180:	deviceOrientation=180;break;
			case Surface.ROTATION_270:	deviceOrientation=270;break;
			}
			totalRotation=(sensorOrientation+deviceOrientation+360)%360;
			//totalRotation=sensorToDeviceRotation(characteristics, deviceOrientation);

			int rotatedWidth, rotatedHeight;
			//boolean transformationNeeded=false;
			if(totalRotation==90||totalRotation==270)
			{
				rotatedWidth=textureHeight;
				rotatedHeight=textureWidth;
				if(sensorOrientation==0||sensorOrientation==180)//experimental, i don't have a landscape android device
				//{
				//	transformationNeeded=true;
					rotatePreview(textureWidth, textureHeight, totalRotation);
				//}
			}
			else//0: landscape upside-down, 180: landscape upright
			{
				rotatedWidth=textureWidth;
				rotatedHeight=textureHeight;
				if(sensorOrientation==90||sensorOrientation==270)
				//{
				//	transformationNeeded=true;
					rotatePreview(textureWidth, textureHeight, 90+totalRotation);
				//}
			}

			int[] supportedFormats=map.getOutputFormats();
			Size[] resolutions=map.getOutputSizes(SurfaceTexture.class);//preview resolutions
			previewSize=resolutions[chooseOptimalSize(resolutions, rotatedWidth, rotatedHeight)];

			//populate imFormatSpinner
			ArrayAdapter<String> adapter=new ArrayAdapter<>(getActivity(), R.layout.spinner_item);//TODO: populate string spinner
			adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
			adapter.add("JPEG");
			adapter.add("PNG");
			if(supportsRaw)
			{
				adapter.add("RAW");
				//if(!transformationNeeded)//if there is a transformation, getBitmap() is distorted
					adapter.add("RAW Preview");
				//adapter.add("RAW+JPEG");
			}
			imFormatSpinner.setAdapter(adapter);
			imFormatSpinner.setSelection(0, false);
			selectedImFormat=ImageFormat.JPEG;

			resolutions=map.getOutputSizes(MediaRecorder.class);//video capture resolutions
			selectedVidRes=chooseOptimalSize(resolutions, rotatedWidth, rotatedHeight);
			vidResList.clear();
			vidResList.addAll(Arrays.asList(resolutions));
			populateResSpinner(vidResSpinner, resolutions, selectedVidRes);
		}
		catch(CameraAccessException e)
		{
			error(1202, e);
		}
	}
	void connectCamera()
	{
		CameraManager manager=(CameraManager)getActivity().getSystemService(Context.CAMERA_SERVICE);
		if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.M&&ActivityCompat.checkSelfPermission(getActivity(), Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED)
		{
			requestPermissions(new String[]{Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO}, CAMERA_REQUEST);
			return;
		}
		try
		{
			manager.openCamera(getCameraId(), new CameraDevice.StateCallback()
			{
				@Override public void onOpened(@NonNull CameraDevice camera)
				{
					cameraDevice=camera;
					cameraOpen=true;
					//displayToast(false, "Camera connection made!");
					if(isRecording)//when asking for permission
					{
						createMediaFilename(MediaType.VIDEO);
						startRecord();
						mediaRecorder.start();
						startChronometer();
					}
					else
						startPreview();
				}
				@Override public void onDisconnected(@NonNull CameraDevice camera)
				{
					camera.close();
					cameraDevice=null;
				}
				@Override public void onError(@NonNull CameraDevice camera, int error)
				{
					camera.close();
					cameraDevice=null;
				}
			}, bkHandler);
		}
		catch(CameraAccessException e)
		{
			error(1246, e);
		}
	}
	void closeCamera()
	{
		if(cameraDevice!=null)
		{
			if(cameraOpen)
				cameraDevice.close();
			cameraDevice=null;
		}
	}
	@Override public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults)
	{
		super.onRequestPermissionsResult(requestCode, permissions, grantResults);
		if(requestCode==CAMERA_REQUEST)
		{
			if(grantResults[0]!=PackageManager.PERMISSION_GRANTED)
				displayRealToast(true, "Need camera permissions to run");
			if(grantResults[1]!=PackageManager.PERMISSION_GRANTED)
				displayRealToast(true, "Need audio permission to record video with sound");
		}
		else if(requestCode==STORAGE_REQUEST)
		{
			if(grantResults[0]!=PackageManager.PERMISSION_GRANTED)
				displayRealToast(true, "Need permission for external storage to save media");
			//else//
			//	displayToast(false, "Storage permission granted");//
		}
	}
	void startPreview()
	{
		SurfaceTexture texture=textureView.getSurfaceTexture();
		if(DEBUG)
			displayToast(false, "Preview resolution: "+previewSize.getWidth()+"x"+previewSize.getHeight());//
		texture.setDefaultBufferSize(previewSize.getWidth(), previewSize.getHeight());
		Surface previewSurface=new Surface(texture);
		try
		{
			requestBuilder=cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
			if(manualMode)
			{
				requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_OFF);
				requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, focus);
				requestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure);
			}
			else
			{
				if(brightnessSupported)
					requestBuilder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, brightnessBias);
				requestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
			}
			if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.R)
			{
				requestBuilder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoomRatio);
			}
			requestBuilder.addTarget(previewSurface);
			cameraDevice.createCaptureSession(Arrays.asList(previewSurface, imageReader.getSurface()), new CameraCaptureSession.StateCallback()
			{
				@Override public void onConfigured(@NonNull CameraCaptureSession session)
				{
					previewCaptureSession=session;
					try
					{
						previewCaptureSession.setRepeatingRequest(requestBuilder.build(), null, bkHandler);
					}
					catch(CameraAccessException e)
					{
						error(1306, e);
					}
				}
				@Override public void onConfigureFailed(@NonNull CameraCaptureSession session)
				{
					error(1311, "Unable to setup camera preview");
				}
			}, null);
		}
		catch(CameraAccessException e)
		{
			error(1318, e);
		}
	}
	void startBkThread()
	{
		bkHandlerThread=new HandlerThread("RawCamDemo");
		bkHandlerThread.start();
		bkHandler=new Handler(bkHandlerThread.getLooper());
	}
	void stopBkThread()
	{
		bkHandlerThread.quitSafely();
		try
		{
			bkHandlerThread.join();
			bkHandlerThread=null;
			bkHandler=null;
		}
		catch(InterruptedException e)
		{
			error(1338, e);
		}
	}

	//file operations
	File folder;
	String filename;
	enum MediaType{IMAGE, VIDEO}
	String makeTimestamp()
	{
		return new SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(new Date());
	}
	File createMediaFilename(MediaType mediaType)//throws IOException
	{
		String extension=".data";
		if(mediaType==MediaType.IMAGE)
		{
			switch(selectedImFormat)
			{
			case ImageFormat.JPEG:			extension=".jpg";break;
			case ImageFormat.RAW_SENSOR:	extension=".dng";break;
			case ImageFormat.FLEX_RGB_888:	extension=".png";break;
			}
		}
		else
			extension=".mp4";//TODO: raw video
		String timestamp=makeTimestamp();
		//String timestamp=new SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(new Date());
		File file=new File(folder, timestamp+extension);//TODO: don't create temp file
		//File file=File.createTempFile(timestamp, extension, folder);
		filename=file.getAbsolutePath();
		displayToast(false, "Saving "+filename);//
		return file;
	}

	//video
	void startChronometer()
	{
		chronometer.setBase(SystemClock.elapsedRealtime());
		chronometer.setVisibility(View.VISIBLE);
		chronometer.start();
	}
	void startRecord()
	{
		try
		{
			float frameRate=30;//TODO: frame rate option
			Size videoSize=getVidRes();
			float pixelRate=(float)videoSize.getWidth()*videoSize.getHeight()*frameRate;
			float compression=(float)Math.log10(pixelRate)*(0.0177f*frameRate+0.8385f);//formula based on https://support.google.com/youtube/answer/2853702?hl=en#zippy=%2Cp%2Ck-p-fps%2Cp-fps
			int bitrate=(int)(pixelRate/compression);
			surfaceView.setStatus(4000, String.format(Locale.US, "Recording %dx%dp%.2f at %.2f Kbps", videoSize.getWidth(), videoSize.getHeight(), frameRate, bitrate*0.001f));

			if(!soundOn)
				mediaRecorder=new MediaRecorder();
			mediaRecorder.setVideoSource(MediaRecorder.VideoSource.SURFACE);
			if(soundOn)
				mediaRecorder.setAudioSource(MediaRecorder.AudioSource.MIC);
			mediaRecorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
			mediaRecorder.setOutputFile(filename);
			mediaRecorder.setVideoEncodingBitRate(bitrate);
			if(soundOn)
				mediaRecorder.setAudioEncodingBitRate(128000);
			mediaRecorder.setVideoFrameRate(30);
			mediaRecorder.setVideoSize(videoSize.getWidth(), videoSize.getHeight());
			mediaRecorder.setVideoEncoder(MediaRecorder.VideoEncoder.H264);
			if(soundOn)
				mediaRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
			mediaRecorder.setOrientationHint(totalRotation);
			mediaRecorder.prepare();

			SurfaceTexture texture=textureView.getSurfaceTexture();
			texture.setDefaultBufferSize(previewSize.getWidth(), previewSize.getHeight());
			Surface previewSurface=new Surface(texture);
			Surface recordSurface=mediaRecorder.getSurface();
			requestBuilder=cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
			requestBuilder.addTarget(previewSurface);
			requestBuilder.addTarget(recordSurface);

			cameraDevice.createCaptureSession(Arrays.asList(previewSurface, recordSurface, imageReader.getSurface()), new CameraCaptureSession.StateCallback()
			{
				@Override public void onConfigured(@NonNull CameraCaptureSession session)
				{
					recordCaptureSession=session;
					try
					{
						recordCaptureSession.setRepeatingRequest(requestBuilder.build(), null, null);
					}
					catch(CameraAccessException e)
					{
						error(1418, e);
					}
				}
				@Override public void onConfigureFailed(@NonNull CameraCaptureSession session){}
			}, null);
		}
		catch(IOException|CameraAccessException e)
		{
			error(1426, e);
		}
	}
}