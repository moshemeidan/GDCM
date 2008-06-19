/*=========================================================================

  Program: GDCM (Grass Root DICOM). A DICOM library
  Module:  $URL$

  Copyright (c) 2006-2008 Mathieu Malaterre
  All rights reserved.
  See Copyright.txt or http://gdcm.sourceforge.net/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkGDCMImageReader.h"

#include "vtkXMLImageDataWriter.h"
#include "vtkPNGWriter.h"
#include "vtkImageShiftScale.h"
#include "vtkOutlineFilter.h"
#include "vtkMath.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkImageReslice.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkImageViewer.h"
#include "vtkPointData.h"
#include "vtkImageMapToColors.h"
#include "vtkLookupTable.h"
#include "vtkActor2D.h"
#include "vtkImageMapToWindowLevelColors.h"
#include "vtkImageActor.h"
#include "vtkWindowToImageFilter.h"
#if VTK_MAJOR_VERSION >= 5 && VTK_MINOR_VERSION > 0
#include "vtkBalloonWidget.h"
#include "vtkBalloonRepresentation.h"
#include "vtkLogoWidget.h"
#include "vtkLogoRepresentation.h"
#include "vtkAngleWidget.h"
#include "vtkAngleRepresentation2D.h"
#include "vtkBiDimensionalWidget.h"
#include "vtkBiDimensionalRepresentation2D.h"
#include "vtkDistanceWidget.h"
#include "vtkContourWidget.h"
#include "vtkOrientedGlyphContourRepresentation.h"
#include "vtkPointHandleRepresentation2D.h"
#include "vtkDistanceRepresentation2D.h"
#include "vtkProperty2D.h"
#else
class vtkLogoWidget;
class vtkBiDimensionalWidget;
class vtkDistanceWidget;
class vtkLogoRepresentation;
class vtkContourWidget;
class vtkAngleWidget;
#endif
#if VTK_MAJOR_VERSION >= 5
#include "vtkImageYBRToRGB.h"
#include "vtkImageColorViewer.h"
#else
#include "vtkImageViewer2.h"
#endif
#include "vtkImageData.h"
#include "vtkCommand.h"
#include "vtkRenderer.h"
#include "vtkStringArray.h"
#include "vtkDebugLeaks.h"
#include "vtkWorldPointPicker.h"

#include "gdcmFilename.h"
#include "gdcmSystem.h"
#include "gdcmDirectory.h"

#include <assert.h>
//----------------------------------------------------------------------------
// vtkImageViewer2 new interface wants SetSlice, but vtkImageViewer does not have
// this new interface (what a pain), so let's fake a new interface to
// vtkImageViewer without patching VTK 
class vtkGDCMImageViewer : public vtkImageViewer
{
public:
  vtkTypeRevisionMacro(vtkGDCMImageViewer,vtkImageViewer);

  static vtkGDCMImageViewer *New()
    { 
#ifdef VTK_DEBUG_LEAKS
    vtkDebugLeaks::ConstructClass("vtkGDCMImageViewer");
#endif
    return new vtkGDCMImageViewer; 
    }
  int GetSlice() { return this->GetZSlice(); }
  void SetSlice(int s) { this->SetZSlice(s); }

  int GetSliceMin() { return this->GetWholeZMin(); }
  int GetSliceMax() { return this->GetWholeZMax(); }
#if VTK_MAJOR_VERSION >= 5
  // TODO:
  void AddInputConnection(vtkAlgorithmOutput* input) {(void)input;}
#else
  void AddInput(vtkImageData * input) {(void)input;}
#endif
  double GetOverlayVisibility() { return 0; }
  void SetOverlayVisibility(double vis) {(void)vis;}
};
vtkCxxRevisionMacro(vtkGDCMImageViewer, "$Revision: 1.30 $")
vtkInstantiatorNewMacro(vtkGDCMImageViewer)

#if VTK_MAJOR_VERSION >= 5
#else
class vtkImageColorViewer : public vtkImageViewer2
{
public:
  vtkTypeRevisionMacro(vtkImageColorViewer,vtkImageViewer2);

  static vtkImageColorViewer *New()
    { 
#ifdef VTK_DEBUG_LEAKS
    vtkDebugLeaks::ConstructClass("vtkImageColorViewer");
#endif
    return new vtkImageColorViewer; 
    }
  vtkImageColorViewer() {
    OverlayImageActor = vtkImageActor::New();
  }
  ~vtkImageColorViewer() {
    OverlayImageActor->Delete();
  }
  double GetOverlayVisibility() { return 0; }
  void SetOverlayVisibility(double vis) {}
  void AddInput(vtkImageData * input)
    {
  vtkRenderWindow *renwin = this->GetRenderWindow ();
  renwin->SetNumberOfLayers(2);
  vtkRenderer *Renderer     = vtkRenderer::New();
  Renderer->SetLayer(0);
  //OverlayImageActor->SetOpacity(0.5);
  //OverlayImageActor->GetProperty()->SetOpacity(1.0);
  vtkImageMapToWindowLevelColors *WindowLevel     = vtkImageMapToWindowLevelColors::New();
  WindowLevel->SetInput(input);
  OverlayImageActor->SetInput(WindowLevel->GetOutput());
  Renderer->AddProp(OverlayImageActor);
  OverlayImageActor->SetVisibility(1);

  renwin->AddRenderer(Renderer);
  Renderer->Delete();
  WindowLevel->Delete();
    }
private:
  vtkImageActor                   *OverlayImageActor;
};
vtkCxxRevisionMacro(vtkImageColorViewer, "$Revision: 1.30 $")
vtkInstantiatorNewMacro(vtkImageColorViewer)
#endif

//----------------------------------------------------------------------------
// Callback for the interaction
template <typename TViewer>
class vtkGDCMObserver : public vtkCommand
{
public:
  static vtkGDCMObserver *New() 
    { 
    return new vtkGDCMObserver; 
    }
  vtkGDCMObserver()
    {
    ImageViewer = NULL;
    IconWidget = NULL;
    DistanceWidget = NULL;
    BiDimWidget = NULL;
    AngleWidget = NULL;
    ContourWidget = NULL;
    picker = vtkWorldPointPicker::New();
    }
  ~vtkGDCMObserver()
    {
    picker->Delete();
    }
  virtual void Execute(vtkObject *caller, unsigned long event, void* /*calldata*/)
    {
    if ( this->ImageViewer )
      {
      vtkRenderWindowInteractor * rwi = vtkRenderWindowInteractor::SafeDownCast( caller );
      if ( event == vtkCommand::CharEvent )
        {
        char keycode = 0;
        if( rwi ) keycode = rwi->GetKeyCode();
        // 'o' is a special key for overlay visibility
        if ( keycode == 'o' )
          {
          ImageViewer->SetOverlayVisibility( 1 - ImageViewer->GetOverlayVisibility() );
          ImageViewer->Render();
          //std::cerr << ImageViewer->GetOverlayVisibility() << std::endl;
          }
        else if ( keycode == 's' )
          {
          vtkPNGWriter * writer = vtkPNGWriter::New();
          vtkWindowToImageFilter * w2i = vtkWindowToImageFilter::New();
          w2i->SetInput( rwi->GetRenderWindow() );
          writer->SetInput( w2i->GetOutput() );
          writer->SetFileName( "snapshot.png" );
          writer->Write();
          writer->Delete();
          w2i->Delete();
          //std::cerr << "Screenshort saved to snapshot.png" << std::endl;
          }
#if VTK_MAJOR_VERSION >= 5 && VTK_MINOR_VERSION > 0
        else if ( keycode == 'l' )
          {
          IconWidget->Off();
          }
        else if ( keycode == 'a' )
          {
          AngleWidget->On();
          }
        else if ( keycode == 'b' )
          {
          BiDimWidget->On();
          }
        else if ( keycode == 'c' )
          {
          ContourWidget->On();
          }
        else if ( keycode == 'd' )
          {
          DistanceWidget->On();
          }
        else if ( keycode == 'p' )
          {
          }
#endif
        else
          {
#if (VTK_MAJOR_VERSION >= 5) || ( VTK_MAJOR_VERSION == 4 && VTK_MINOR_VERSION > 5 )
          int max = ImageViewer->GetSliceMax();
          int slice = (ImageViewer->GetSlice() + 1) % ++max;
          ImageViewer->SetSlice( slice );
#else
          int max = ImageViewer->GetWholeZMax();
          int slice = (ImageViewer->GetZSlice() + 1 ) % ++max;
          ImageViewer->SetZSlice( slice );
          ImageViewer->GetRenderer()->ResetCameraClippingRange();
#endif
          ImageViewer->Render();
          }
        }
      else if ( event == vtkCommand::EndPickEvent )
        {
        //std::cerr << "EndPickEvent" << std::endl;
        int *pick = rwi->GetEventPosition();
        vtkRenderer *ren1 = ImageViewer->GetRenderer();
        picker->Pick((double)pick[0], (double)pick[1], 0.0, ren1);
#if (VTK_MAJOR_VERSION >= 5) || ( VTK_MAJOR_VERSION == 4 && VTK_MINOR_VERSION > 2 )
        double *pos = picker->GetPickPosition ();
#else
        float *pos = picker->GetPickPosition ();
#endif
        std::cout << pos[0] << "," << pos[1] << "," << pos[2] << std::endl;
        }
      else
        {
        std::cerr << "Unhandled even:" << event << std::endl;
        }
      }
    }
  TViewer *ImageViewer;
  vtkWorldPointPicker *picker;
  vtkLogoWidget *IconWidget;
  vtkDistanceWidget *DistanceWidget;
  vtkAngleWidget *AngleWidget;
  vtkBiDimensionalWidget *BiDimWidget;
  vtkContourWidget *ContourWidget;

};

class vtkBalloonCallback : public vtkCommand
{
public:
  static vtkBalloonCallback *New() 
    { return new vtkBalloonCallback; }
  virtual void Execute(vtkObject *caller, unsigned long, void*)
    {
      vtkBalloonWidget *balloonWidget = reinterpret_cast<vtkBalloonWidget*>(caller);
      if ( balloonWidget->GetCurrentProp() != NULL )
        {
        cout << "Prop selected\n";
        }
    }
};


// A feature in VS6 make it painfull to write template code
// that do not contain the template parameter in the function
// signature. Thus always pass parameter in the function:
template <typename TViewer>
void ExecuteViewer(TViewer *viewer, vtkStringArray *filenames)
{
  vtkGDCMImageReader *reader = vtkGDCMImageReader::New();
  if( filenames->GetSize() == 1 ) // Backward compatible...
    {
    reader->SetFileName( filenames->GetValue(0) );
    }
  else
    {
    reader->SetFileNames( filenames );
    }

  vtkRenderWindowInteractor *iren = vtkRenderWindowInteractor::New();
  // For a single medical image, it would be more efficient to use
  // 0028|1050 [DS] [Window Center]
  // 0028|1051 [DS] [Window Width]
  // but gdcmviewer doesn't know about them :-(

  //reader->FileLowerLeftOn();
  reader->Update();
  //reader->Print( cout );
  reader->GetOutput()->Print( cout );
  //reader->GetOutput(1)->Print( cout );
#if (VTK_MAJOR_VERSION >= 5) || ( VTK_MAJOR_VERSION == 4 && VTK_MINOR_VERSION > 2 )
  double range[2];
#else
  float range[2];
#endif /*(VTK_MAJOR_VERSION >= 5) || ( VTK_MAJOR_VERSION == 4 && VTK_MINOR_VERSION > 5 )*/
  reader->GetOutput()->GetScalarRange(range);
#if (VTK_MAJOR_VERSION >= 5) || ( VTK_MAJOR_VERSION == 4 && VTK_MINOR_VERSION > 5 )
  viewer->SetInputConnection ( reader->GetOutputPort(0) );
  // Technically we could just simple always call AddInputConnection on the overlay
  // but there is something wrong going on, when overlay visibility is set to 0 at startup
  // thus we need to make it visible by default which is annoying, so activate only
  // if overlays are found:
  if( reader->GetNumberOfOverlays() )
    {
    // Add first overlay:
    // WARNING: gdcmviewer2 only !
    viewer->AddInputConnection ( reader->GetOverlayPort(0) );
    }
  // TODO: Icon can be added using the vtkLogoWidget
#if VTK_MAJOR_VERSION >= 5 && VTK_MINOR_VERSION > 0
  vtkPointHandleRepresentation2D *handle = vtkPointHandleRepresentation2D::New();
  handle->GetProperty()->SetColor(1,0,0);
  vtkDistanceRepresentation2D *rep = vtkDistanceRepresentation2D::New();
  rep->SetHandleRepresentation(handle);
  handle->Delete();

  vtkDistanceWidget *dwidget = vtkDistanceWidget::New();
  dwidget->SetInteractor(iren);
  dwidget->CreateDefaultRepresentation();
  dwidget->SetRepresentation(rep);
  rep->Delete();

  vtkAngleRepresentation2D *anglerep = vtkAngleRepresentation2D::New();
  vtkAngleWidget *anglewidget = vtkAngleWidget::New();
  anglewidget->SetInteractor(iren);
  anglewidget->SetRepresentation(anglerep);
  anglerep->Delete();

  vtkBiDimensionalRepresentation2D *brep = vtkBiDimensionalRepresentation2D::New();
  vtkBiDimensionalWidget *bwidget = vtkBiDimensionalWidget::New();
  bwidget->SetInteractor(iren);
  bwidget->SetRepresentation(brep);
  brep->Delete();

  vtkOrientedGlyphContourRepresentation *contourRep = vtkOrientedGlyphContourRepresentation::New();
  vtkContourWidget *contourWidget = vtkContourWidget::New();
  contourWidget->SetInteractor(iren);
  contourWidget->SetRepresentation(contourRep);
  contourRep->Delete();

  vtkBalloonRepresentation *balloonrep = vtkBalloonRepresentation::New();
  balloonrep->SetBalloonLayoutToImageRight();

  vtkBalloonWidget *balloonwidget = vtkBalloonWidget::New();
  balloonwidget->SetInteractor(iren);
  balloonwidget->SetRepresentation(balloonrep);
  balloonwidget->AddBalloon(viewer->GetImageActor(),"This is a DICOM image",NULL);

  vtkBalloonCallback *cbk = vtkBalloonCallback::New();
  balloonwidget->AddObserver(vtkCommand::WidgetActivateEvent,cbk);

  vtkLogoWidget * iconwidget = 0;
  if( reader->GetNumberOfIconImages() )
    {
    vtkLogoRepresentation *rep = vtkLogoRepresentation::New();
    rep->SetImage(reader->GetIconImage());
    //reader->GetIconImage()->Print( std::cout );

    //vtkPropCollection *pc = vtkPropCollection::New();
    //rep->GetActors2D(pc);
    //balloonwidget->AddBalloon(pc->GetLastProp(),"This is a DICOM thumbnail image",NULL);

    vtkLogoWidget *widget = vtkLogoWidget::New();
    widget->SetInteractor(iren);
    widget->SetRepresentation(rep);
    iconwidget = widget;
    //widget->Delete();
    rep->Delete();

    //viewer->AddInputConnection ( reader->GetIconImagePort() );
    }
#endif
#else
  viewer->SetInput( reader->GetOutput(0) );
  if( reader->GetNumberOfOverlays() )
    {
    viewer->AddInput( reader->GetOverlay(0) );
    }
#endif /*(VTK_MAJOR_VERSION >= 5) || ( VTK_MAJOR_VERSION == 4 && VTK_MINOR_VERSION > 5 )*/

  // IconImage:
  //if( reader->GetNumberOfIconImages() )
  //  {
  //  std::cerr << "NumberOfIconImages:" << reader->GetNumberOfIconImages() << std::endl;
  //  reader->GetIconImage()->Print( std::cerr );
  //  vtkPNGWriter *writer = vtkPNGWriter::New();
  //  writer->SetInput( reader->GetIconImage() );
  //  writer->SetFileName( "icon.png" );
  //  //writer->Write();
  //  writer->Delete();
  //  }

  // In case of palette color, let's tell VTK to map color:
  // MONOCHROME1 is also implemented with a lookup table
  if( reader->GetImageFormat() == VTK_LOOKUP_TABLE || reader->GetImageFormat() == VTK_INVERSE_LUMINANCE )
    {
    assert( reader->GetOutput()->GetPointData()->GetScalars() 
      && reader->GetOutput()->GetPointData()->GetScalars()->GetLookupTable() );
    //convert to color:
    vtkImageMapToColors *map = vtkImageMapToColors::New ();
    map->SetInput (reader->GetOutput());
    map->SetLookupTable (reader->GetOutput()->GetPointData()->GetScalars()->GetLookupTable());
    if( reader->GetImageFormat() == VTK_LOOKUP_TABLE )
      {
      map->SetOutputFormatToRGB();
      }
    else if( reader->GetImageFormat() == VTK_INVERSE_LUMINANCE )
      {
      map->SetOutputFormatToLuminance();
      }
    map->Update();
    map->GetOutput()->GetScalarRange(range);
    viewer->SetInput( map->GetOutput() );
    map->Delete();
    }
  else if( reader->GetImageFormat() == VTK_YBR )
    {
#if VTK_MAJOR_VERSION >= 5
    vtkImageYBRToRGB *filter = vtkImageYBRToRGB::New();
    filter->SetInput( reader->GetOutput() );
    filter->Update();
    filter->GetOutput()->GetScalarRange(range);
    viewer->SetInput( filter->GetOutput() );
    filter->Delete();
#else
    std::cerr << "Not implemented" << std::endl;
#endif
    }
//  vtkImageShiftScale *ss = vtkImageShiftScale::New();
//  ss->SetInput( reader->GetOutput() );
//  ss->SetShift( -1024 );
//  ss->SetOutputScalarTypeToShort();
//  ss->Update();
//    ss->GetOutput()->GetScalarRange(range);
//    viewer->SetInput( ss->GetOutput() );
//    ss->Delete();

  if( reader->GetCurve() )
    {
    vtkPolyDataMapper2D * rectMapper = vtkPolyDataMapper2D::New();
    rectMapper->SetInput( reader->GetCurve() );

    vtkActor2D * rectActor = vtkActor2D::New();
    rectActor->SetMapper( rectMapper );
    viewer->GetRenderer()->AddActor2D( rectActor );
    rectActor->Delete();
    rectMapper->Delete();
    }

#if 0
  vtkImageReslice * slicer = vtkImageReslice::New();
  slicer->SetInput( reader->GetOutput() );
  slicer->InterpolateOn();
  //slicer->SetResliceAxesOrigin(0, 0, 0);
  //slicer->SetResliceAxesOrigin( reader->GetImagePositionPatient() );
  //slicer->SetResliceAxes( reader->GetDirectionCosines() );
  const double *dircos = reader->GetImageOrientationPatient();
  double dcos[9];
  for(int i=0;i<6;++i)
    dcos[i] = dircos[i];
  dcos[6] = dircos[1] * dircos[5] - dircos[2] * dircos[4];
  dcos[7] = dircos[2] * dircos[3] - dircos[0] * dircos[5];
  dcos[8] = dircos[0] * dircos[4] - dircos[3] * dircos[1];
  double dummy[3];
  double dot = vtkMath::Dot(dircos, dircos+3);
  std::cout << dot << std::endl;
  vtkMath::Cross(dircos, dircos+3, dummy);
  std::cout << dcos[6] << "," << dcos[7] << "," << dcos[8] << std::endl;
  std::cout << dummy[0] << "," << dummy[1] << "," << dummy[2] << std::endl;
  dot = vtkMath::Dot(dircos, dummy);
  std::cout << dot << std::endl;
  dot = vtkMath::Dot(dircos+3, dummy);
  std::cout << dot << std::endl;
  slicer->SetResliceAxesDirectionCosines(dcos);
  slicer->Update();
  slicer->GetOutput()->Print( std::cout );
  //viewer->SetInput( slicer->GetOutput() );
  vtkOutlineFilter * outline = vtkOutlineFilter::New();
  outline->SetInput( slicer->GetOutput() );
  outline->GetOutput()->Print( std::cout );
  //slicer->AddInput( (vtkPolyData*)outline->GetOutput() );
#endif

  // Always overwriting default is not always nice looking...
  viewer->SetColorLevel (0.5 * (range[1] + range[0]));
  viewer->SetColorWindow (range[1] - range[0]);
  std::cerr << "Range: " << range[0] << " " << range[1] << std::endl;

  viewer->SetupInteractor (iren);
  int dims[3];
  reader->GetOutput()->GetDimensions(dims);
  // Make sure to display on most screen
  dims[0] = (dims[0] < 600 ) ? dims[0] : 600;
  dims[1] = (dims[1] < 600 ) ? dims[1] : 600;
  viewer->Render(); // EXTREMELY IMPORTANT for vtkImageViewer2

  viewer->SetSize( dims );

  // Here is where we setup the observer, 
  vtkGDCMObserver<TViewer> *obs = vtkGDCMObserver<TViewer>::New();
  obs->ImageViewer = viewer;
#if VTK_MAJOR_VERSION >= 5 && VTK_MINOR_VERSION > 0
  if(iconwidget) iconwidget->On();
  obs->IconWidget = iconwidget;
  obs->DistanceWidget = dwidget;
  obs->AngleWidget = anglewidget;
  obs->BiDimWidget = bwidget;
  obs->ContourWidget = contourWidget;
  balloonwidget->On();
#endif
  iren->AddObserver(vtkCommand::CharEvent,obs);
  iren->AddObserver(vtkCommand::EndPickEvent,obs);
  obs->Delete();

  iren->Initialize();
  iren->Start();

  //if you wish you can export dicom to a vtk file
#if 0
  vtkXMLImageDataWriter *writer = vtkXMLImageDataWriter::New();
  writer->SetInputConnection( reader->GetOutputPort());
  writer->SetFileName( "debug.vtk" );
  writer->SetDataModeToBinary();
  writer->Write();
  writer->Delete();
#endif

#if 0
  vtkPNGWriter *writer = vtkPNGWriter::New();
  //writer->SetInputConnection( reader->GetOutputPort() );
  writer->SetInputConnection( reader->GetOutputPort(1) );
  //range = overlay->GetScalarRange();
  //std::cerr << "Range: " << range[0] << " " << range[1] << std::endl;
  //overlay->Print( std::cout );
  //writer->SetInput( overlay );
  writer->SetFileName( "debug.png" );
  writer->Write();
  writer->Delete();
#endif

  reader->Delete();
#if VTK_MAJOR_VERSION >= 5 && VTK_MINOR_VERSION > 0
  dwidget->Off();
  balloonwidget->Off();
  balloonwidget->Delete();
  dwidget->Delete();
  anglewidget->Off();
  anglewidget->Delete();
  bwidget->Off();
  bwidget->Delete();
  contourWidget->Off();
  contourWidget->Delete();
  if( iconwidget )
    {
    iconwidget->Off();
    iconwidget->Delete();
    }
#endif
  iren->Delete();
  viewer->Delete();
}

int main(int argc, char *argv[])
{
  vtkStringArray *filenames = vtkStringArray::New();
  if( argc < 2 )
    {
    std::cerr << argv[0] << " filename1.dcm [filename2.dcm ...]\n";
    return 1;
    }
  else
    {
    // Is it a single directory ? If so loop over all files contained in it:
    const char *filename = argv[1];
    if( argc == 2 && gdcm::System::FileIsDirectory( filename ) )
      {
      std::cout << "Loading directory: " << filename << std::endl;
      bool recursive = false;
      gdcm::Directory d;
      d.Load(filename, recursive);
      gdcm::Directory::FilenamesType const &files = d.GetFilenames();
      for( gdcm::Directory::FilenamesType::const_iterator it = files.begin(); it != files.end(); ++it )
        {
        filenames->InsertNextValue( it->c_str() );
        }
      }
    else // list of files passed directly on the cmd line:
        // discard non-existing or directory
      {
      for(int i=1; i < argc; ++i)
        {
        filename = argv[i];
        if( gdcm::System::FileExists( filename ) )
          {
          if( gdcm::System::FileIsDirectory( filename ) )
            {
            std::cerr << "Discarding directory: " << filename << std::endl;
            }
          else
            {
            filenames->InsertNextValue( filename );
            }
          }
        else
          {
          std::cerr << "Discarding non existing file: " << filename << std::endl;
          }
        }
      }
    //names->Print( std::cout );
    }

  vtkImageColorViewer *viewer = vtkImageColorViewer::New();
  ExecuteViewer<vtkImageColorViewer>(viewer, filenames);

  filenames->Delete();

  return 0;
}

