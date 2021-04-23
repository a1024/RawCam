package com.example.rawcamdemo;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.DngCreator;
import android.media.Image;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;

import androidx.annotation.RequiresApi;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class ImageSaver2
{
	static class Stream
	{
		OutputStream stream;
		String filename;
	}
	static String getName(String filename)
	{
		int k=filename.length()-1;//get name
		for(;k>=0;--k)
		{
			if(filename.charAt(k)==File.separator.charAt(0))
			{
				++k;
				break;
			}
		}
		return filename.substring(k);
	}
	static String getExtension(String filename)
	{
		int k=filename.length()-1;//get name
		for(;k>=0;--k)
		{
			if(filename.charAt(k)=='.')
			{
				++k;
				break;
			}
		}
		return filename.substring(k);
	}

	CameraFragment parent;
	void init(CameraFragment _parent)
	{
		parent=_parent;
	}
	@RequiresApi(api=Build.VERSION_CODES.Q)
	//OutputStream getStream(String name, String extension)
	Stream getStream(String name)
	{
		Context context=parent.getActivity();
		if(context==null)
		{
			parent.saveError(37, "activity == null");
			return null;
		}
		ContentResolver resolver=context.getContentResolver();
		ContentValues values=new ContentValues();
		values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
		String extension=getExtension(name);
		Uri uri=null;
		String filename;
		if(extension.contentEquals("jpg"))
		{
			values.put(MediaStore.MediaColumns.MIME_TYPE, "image/jpg");
			values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DCIM+File.separator+"Raw");
			uri=resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
			filename="DCIM/"+name;
		}
		else
		{
			values.put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream");
			values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOCUMENTS+File.separator+"Raw");
			uri=resolver.insert(MediaStore.Files.getContentUri("external"), values);
			filename="Documents/"+name;
		}
		try
		{
			Stream s=new Stream();
			s.stream=resolver.openOutputStream(uri);
			s.filename=filename;
			return s;
		}
		catch(IOException e)
		{
			parent.saveError(83, e);
		}
		return null;


	/*	File file=new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM), "Raw"+File.separator+name);
		try
		{
			boolean exists=file.createNewFile();//operation not permitted
			return new FileOutputStream(file);
		}
		catch(IOException e)
		{
			parent.saveError(77, e);
		}
		return null;//*/

	/*	Context context=parent.getActivity();
		if(context==null)
		{
			parent.saveError(37, "activity == null");
			return null;
		}

		ContentResolver resolver=context.getContentResolver();
		ContentValues values=new ContentValues();


		values.put(MediaStore.Files.FileColumns.DISPLAY_NAME, name);
		values.put(MediaStore.Files.FileColumns.RELATIVE_PATH, Environment.DIRECTORY_DCIM+File.separator+"Raw");
		//Uri fileUri=resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);//CRASH


	//	values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
	//	//values.put(MediaStore.MediaColumns.MIME_TYPE, extension);
	//	values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DCIM+File.separator+"Raw");
	//	Uri fileUri=resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);//CRASH

		try
		{
			return resolver.openOutputStream(Objects.requireNonNull(fileUri));
		}
		catch(IOException e)
		{
			parent.saveError(52, e);
		}
		return null;//*/
	}

	String saveBinary(final String filename, byte[] data)
	{
		String result=null;
		if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.Q)//https://www.youtube.com/watch?v=tYQ8AO58Aj0
		{
			String name=getName(filename);
			Stream s=getStream(name);
			if(s==null)
				return null;
			try
			{
				s.stream.write(data);
				result=s.filename;
			}
			catch(IOException e)
			{
				parent.saveError(79, e);
			}
			try
			{
				s.stream.close();
			}
			catch(IOException e)
			{
				parent.saveError(161, e);
			}
		}
		else//legacy permissions, before android 10
		{
			FileOutputStream stream=null;
			try
			{
				stream=new FileOutputStream(filename);
				stream.write(data);
				result=filename;
			}
			catch(IOException e)
			{
				parent.saveError(75, e);
			}
			finally
			{
				if(stream!=null)
				{
					try
					{
						stream.close();
					}
					catch(IOException e)
					{
						parent.saveError(87, e);
					}
				}
			}
		}
		return result;
	}
	String saveDng(final String filename, final Image image, final CaptureResult captureResult, final CameraCharacteristics characteristics)
	{
		String result=null;
		DngCreator dngCreator=new DngCreator(characteristics, captureResult);
		if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.Q)//https://www.youtube.com/watch?v=tYQ8AO58Aj0
		{
			String name=getName(filename);
			Stream s=getStream(name);
			if(s.stream==null)
				return null;
			try
			{
				dngCreator.writeImage(s.stream, image);
				result=s.filename;
			}
			catch(IOException e)
			{
				parent.saveError(165, e);
			}
			try
			{
				s.stream.close();
			}
			catch(IOException e)
			{
				parent.saveError(173, e);
			}
		}
		else//legacy permissions, before android 10
		{
			FileOutputStream stream=null;
			try
			{
				stream=new FileOutputStream(filename);
				dngCreator.writeImage(stream, image);
				result=filename;
			}
			catch(IOException e)
			{
				parent.saveError(176, e);//either handle or throw exception
				//printFailure();
			}
			finally
			{
				//image.close();
				if(stream!=null)
				{
					try
					{
						stream.close();
					}
					catch(IOException e)
					{
						parent.saveError(190, e);
					}
				}
			}
		}
		return result;
	}
}
