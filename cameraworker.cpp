#include "cameraworker.h"
#include <highgui/highgui.hpp>
#include <core/core.hpp>
#include <imgproc/imgproc.hpp>
#include <opencv.hpp>
#include <QtCore>
#define CAMNUM 0
CameraWorker::CameraWorker(QObject *parent) :
    QObject(parent)
{
    isCamera = true;
    isThreshold = true;
    isArenaCalib = false;
    isTab2 = false;
    isBlob = true;
    isShape = true;
    isPrimaryShape = true;
    lHeight = 480;
    lWidth = 640;
    delx = dely = 0;
    zoom = 0;
    b = new Blob(cvSize(640, 480));
    zoomArr = new float[7]{1,1.5, 2.25, 3.375, 5.0625, 7.59375, 11.390625};
    capture = cvCaptureFromCAM(CAMNUM);
    if(!capture)
    {
        qDebug() << "Error! camera could not be initialized";
        return;
    }
//    cvSetCaptureProperty(capture, CV_CAP_PROP_FPS, 30);
    cvSetCaptureProperty(capture, CV_CAP_PROP_BRIGHTNESS, -1.0);
    cvSetCaptureProperty(capture, CV_CAP_PROP_SATURATION, 0.8);
    IplImage *temp = cvLoadImage("pics/Picture 25.jpg");
    arenaFrame = cvCreateImage(cvSize(640,480), 8, 3);
    cvResize(temp, arenaFrame);
    if(!arenaFrame)
    {
        qDebug() << "Arena couln't be loaded.";
        return;
    }
    frame = cvQueryFrame(capture);
//        qDebug() << "no roi";
    roi = new IplROI;
    roi->height=frame->height;
    roi->width=frame->width;
    roi->xOffset=0;
    roi->yOffset=0;
    frame->roi = roi;
    if(frame->height != 480 || frame->width != 640)
    {
        qDebug() << "Error! height/width not 640x480!";
        return;
    }
    bs = new BeliefState;
    scd = new ShapeContextDescriptor;
    matcher = new Matcher();
//    qDebug() << "xOffset = " << frame->roi->xOffset << " yOffset = " << frame->roi->yOffset << " width= " << frame->roi->width << "height=" << frame->roi->height;
    displayCamFrame = cvCreateImage(cvSize(frame->width, frame->height), 8, 3);
    displayArenaFrame = cvCloneImage(arenaFrame);
    calibFrame = cvCreateImage(cvSize(frame->width, frame->height), 8, 3);
    blobImage = cvCreateImage(cvSize(frame->width, frame->height), 8, 1);
    myPixmap = NULL;
    descriptorVisualPixmap = NULL;
    descriptorBinaryPixmap = NULL;
    timer = new QTimer;
    connect(timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

void CameraWorker::setup(QThread *cThread, QMutex *mutex, QMutex *_lutMutex, LUT3D *_lut)
{
    myThread = cThread;
    myMutex = mutex;
    lutMutex = _lutMutex;
    lut = _lut;
    emit shapeListChanged(matcher->getTrainingSet());
    connect(cThread, SIGNAL(started()), this, SLOT(onEntry()));
}

CameraWorker::~CameraWorker()
{
    if(matcher)
        delete matcher;
}

void CameraWorker::onTimeout()
{
//    qDebug() << "timeout!";
    if(isCamera)
    {
        displayFrame = displayCamFrame;
        frame = cvQueryFrame(capture);
    }
    else
    {
        displayFrame = displayArenaFrame;
        frame = arenaFrame;
    }
    if(!frame)
        return;
    frame->roi = roi;
    cvResize(frame, calibFrame, CV_INTER_NN);
    cvCopy(calibFrame, displayFrame);
    if(isThreshold)
        colorImage(calibFrame, displayFrame);    
    if(isBlob)
    {
        makeBlobImage(frame, blobImage);
        b->detectBlobs(blobImage, a.getZoneImage());
        blobDataArr = b->getBlobDataArr();
        drawBlobs(displayFrame, blobDataArr);
//        myMutex->lock();
//        bs->populateFromBlobData(blobDataArr);
//        bs->bombDepositPoint = a.getBombDrop();
//        bs->resourceDepositPoint = a.getMineDrop();
//        bs->startCorner = a.getStartCorner();
//        myMutex->unlock();
//        emit beliefStateReady(bs);
    }
    if(isArenaCalib)
    {
        a.drawArenaDisplay(displayFrame);
    }
    if(isShape && isBlob)
    {
        primaryShape = Shape();
        secondaryShape = Shape();
        double primaryMinDistSqr=0, secondaryMinDistSqr = 0;
        IplImage *primaryDescriptorBinaryCopy = NULL, *secondaryDescriptorBinaryCopy = NULL;
        descriptorVisual = NULL;
        if(isBlob && blobDataArr.size() !=0)
        {
//            qDebug() << "tab2";
            scd->calcDescriptor(blobImage, blobDataArr[0]);
            primaryDescriptor = scd->getDescriptor();
            primaryDescriptorBinaryCopy = cvCloneImage(scd->displayBinaryImage());
            if(isPrimaryShape)
                descriptorVisual = scd->displayDescriptorVisual();
            if(matcher->getTrainedSetSize() != 0)
            {
                primaryShape = matcher->getMatch(primaryDescriptor);
                primaryMinDistSqr = matcher->getMinDistSqr();
            }
            else
                primaryShape = Shape();
            if(blobDataArr.size() > 1)
            {
                scd->calcDescriptor(blobImage, blobDataArr[1]);
                secondaryDescriptor = scd->getDescriptor();
                secondaryDescriptorBinaryCopy = cvCloneImage(scd->displayBinaryImage());
                if(!isPrimaryShape)
                    descriptorVisual = scd->displayDescriptorVisual();
                if(matcher->getTrainedSetSize() != 0)
                {
                    secondaryShape = matcher->getMatch(secondaryDescriptor);
                    secondaryMinDistSqr = matcher->getMinDistSqr();
                }
                else
                    secondaryShape = Shape();
            }
            if(isPrimaryShape && primaryDescriptorBinaryCopy)
            {
                IplImage *descriptorBinary = primaryDescriptorBinaryCopy;
                cvCvtColor(descriptorBinary, descriptorBinary, CV_RGB2BGR);
                QImage qimg((uchar*)descriptorBinary->imageData, descriptorBinary->width, descriptorBinary->height, descriptorBinary->widthStep, QImage::Format_RGB888);
                myMutex->lock();
                if(descriptorBinaryPixmap)
                    delete descriptorBinaryPixmap;
                descriptorBinaryPixmap = new QPixmap(QPixmap::fromImage(qimg));
                myMutex->unlock();
                emit descriptorBinaryReady(descriptorBinaryPixmap);
//                cvReleaseImage(&descriptorBinary);
//                primaryDescriptorBinaryCopy = NULL;
            }
            if(!isPrimaryShape && secondaryDescriptorBinaryCopy)
            {
                IplImage *descriptorBinary = secondaryDescriptorBinaryCopy;
                cvCvtColor(descriptorBinary, descriptorBinary, CV_RGB2BGR);
                QImage qimg((uchar*)descriptorBinary->imageData, descriptorBinary->width, descriptorBinary->height, descriptorBinary->widthStep, QImage::Format_RGB888);
                myMutex->lock();
                if(descriptorBinaryPixmap)
                    delete descriptorBinaryPixmap;
                descriptorBinaryPixmap = new QPixmap(QPixmap::fromImage(qimg));
                myMutex->unlock();
                emit descriptorBinaryReady(descriptorBinaryPixmap);
//                cvReleaseImage(&descriptorBinary);
//                secondaryDescriptorBinaryCopy = NULL;
            }
            if(primaryDescriptorBinaryCopy)
                cvReleaseImage(&primaryDescriptorBinaryCopy);
            if(secondaryDescriptorBinaryCopy)
                cvReleaseImage(&secondaryDescriptorBinaryCopy);
            debugData = QString("");            
        }
        else
        {
            debugData = QString("Blob not set, or blobDataSize= 0!");
        }
        emit currentShape(primaryShape/*(QString)Shape::shapeNamesDefault[curShape.st]*/, primaryMinDistSqr);
        emit secondaryShapeSignal(secondaryShape, secondaryMinDistSqr);
//        descriptorVisual = scd->displayDescriptorVisual();
//        cvCvtColor(descriptorVisual, descriptorVisual,CV_BGR2RGB);
        if(descriptorVisual)
        {
            QImage qimg((uchar*)descriptorVisual->imageData, descriptorVisual->width, descriptorVisual->height, descriptorVisual->widthStep, QImage::Format_Indexed8);
            myMutex->lock();
            if(descriptorVisualPixmap)
                delete descriptorVisualPixmap;
            descriptorVisualPixmap = new QPixmap(QPixmap::fromImage(qimg));
            myMutex->unlock();
            emit descriptorVisualReady(descriptorVisualPixmap);
        }

        myMutex->lock();
        bs->populateFromShapes(primaryShape, secondaryShape);
//        bs->bombDepositPoint = a.getBombDrop();
//        bs->resourceDepositPoint = a.getMineDrop();
//        bs->startCorner = a.getStartCorner();
        myMutex->unlock();
        emit beliefStateReady(bs);
    }
    cvCvtColor(displayFrame, displayFrame,CV_BGR2RGB);
    QImage qimg((uchar*)displayFrame->imageData, displayFrame->width, displayFrame->height, displayFrame->widthStep, QImage::Format_RGB888);
    myMutex->lock();
    if(myPixmap)
        delete myPixmap;
    myPixmap = new QPixmap(QPixmap::fromImage(qimg));
    myMutex->unlock();
    emit imageReady(myPixmap);
    emit sendDebugData(debugData);
    timer->setSingleShot(true);
    timer->start(10);
}

void CameraWorker::onEntry()
{
    qDebug() << "Entered Camera Worker.";
    timer->setSingleShot(true);
    timer->start(10);
}

void CameraWorker::onStop()
{
    if(capture)
        cvReleaseCapture(&capture);
    if(displayCamFrame)
        cvReleaseImage(&displayCamFrame);
    if(displayArenaFrame)
        cvReleaseImage(&displayArenaFrame);
    myMutex->lock();
    if(myPixmap)
        delete myPixmap;
    myMutex->unlock();
    qDebug() << "Stopped camera thread.";
    myThread->exit();
}

void CameraWorker::onRadioToggle(bool cam)
{
    isCamera = cam;
    if(!cam)
    {
        cvReleaseCapture(&capture);
    }
    else
        capture = cvCaptureFromCAM(CAMNUM);
}

void CameraWorker::onThresholdChanged(bool val)
{
    isThreshold = val;
}

void CameraWorker::onBlobChanged(bool val)
{
    isBlob = val;
}

void CameraWorker::onShapeChanged(int val)
{
    isShape = val;
    if(isShape)
        qDebug()<<"shape!";
    else
        qDebug()<<"no shape";
}

void CameraWorker::onTabChanged(bool val)
{
    isTab2 = val;
//    if(isTab2)
//        qDebug() << "ON tab2!";
//    else
//        qDebug() << "not on tab2...";
}

void CameraWorker::onArenaCheckBoxToggled(bool checked)
{
    if(checked && !isArenaCalib)
        onMouseScrolled(-zoom);
    isArenaCalib = checked;
}

void CameraWorker::onMouseDraggedArenaCalib(int x, int y)
{
    if(isArenaCalib)
        a.updateOnMouseDrag(x, y);
}

void CameraWorker::onMouseClickedArenaCalib(int x, int y)
{
    if(isArenaCalib)
        a.updateOnMouseClick(x, y);
}

CvPoint CameraWorker::convertToCalibCoord(CvPoint in)
{
    CvPoint ret;
    ret.x = in.x - roi->xOffset;
    ret.y = in.y - roi->yOffset;
    ret.x *= lWidth / (float)roi->width;
    ret.y *= lHeight / (float)roi->height;
    return ret;
}

void CameraWorker::onMouseClicked(int x, int y)//WARNING: calibration now done on displayframe directly?
{
    if(isArenaCalib)
        return;
    if(!frame)
        return;
    if(x >= frame->width || y>=frame->height || x<0 || y<0)
        return;
    int b = IMGDATA(calibFrame, y, x, 0);
    int g = IMGDATA(calibFrame, y, x, 1);
    int r = IMGDATA(calibFrame, y, x, 2);
    emit markBGR(b, g, r);
}

void CameraWorker::onRightMouseDragged(int dx, int dy)
{
    if(isArenaCalib)
        return;
    roi->xOffset-=dx/(zoom+1);
    roi->yOffset-=dy/(zoom+1);
    if(roi->xOffset < 0)
        roi->xOffset = 0;
    else if(roi->xOffset + roi->width > lWidth)
        roi->xOffset = lWidth - roi->width;
    if(roi->yOffset < 0)
        roi->yOffset = 0;
    else if(roi->yOffset + roi->height > lHeight)
        roi->yOffset = lHeight - roi->height;
}

void CameraWorker::onMouseScrolled(int ticks)//WARNING: currently only considers ticks as positive/negative
{
    if(isArenaCalib)
        return;
    if(zoom + ticks < 0 || zoom + ticks > 6)
        return;
    zoom += ticks;
    int newWidth, newHeight;
    if(zoom > 0)
    {
        newWidth = lWidth /zoomArr[zoom];
        newHeight = lHeight /zoomArr[zoom];
    }
    else
    {
        newWidth = lWidth *zoomArr[zoom];
        newHeight = lHeight *zoomArr[zoom];
    }
    roi->xOffset = roi->xOffset + roi->width/2 - newWidth/2;
    roi->yOffset = roi->yOffset + roi->height/2 - newHeight/2;
    roi->width = newWidth;
    roi->height = newHeight;
    if(roi->xOffset < 0)
        roi->xOffset = 0;
    else if(roi->xOffset + roi->width > lWidth)
        roi->xOffset = lWidth - roi->width;
    if(roi->yOffset < 0)
        roi->yOffset = 0;
    else if(roi->yOffset + roi->height > lHeight)
        roi->yOffset = lHeight - roi->height;
}


void CameraWorker::colorImage(IplImage *in, IplImage *out)
{
    if(!in)
    {
        printf("input is empty.\n");
        return;
    }
    if(in->height != out->height || in->width != out->width || in->nChannels != out->nChannels)
    {
        printf("input - output sizes dont match!\n");
        return;
    }
    lutMutex->lock();
    for (int i = 0; i < in->height; ++i)
    {
        for (int j = 0; j < in->width; ++j)
        {
            uchar *pin = &IMGDATA(in, i, j, 0);
            int b = *pin;
            int g = *(pin+1);
            int r = *(pin+2);
            CvScalar cs = lut->getScalar(lut->lookup(b, g, r));
            if(lut->lookup(b, g, r) != UNDEF)
            {
                  uchar *pout = &IMGDATA(out, i, j, 0);
                  *pout = cs.val[0];
                  *(pout+1)=cs.val[1];
                  *(pout+2)=cs.val[2];
            }
        }
    }
    lutMutex->unlock();
}

void CameraWorker::makeBlobImage(IplImage *src, IplImage *dst)
{
    if(!src || !dst)
    {
        printf("src/dst is empty.\n");
        return;
    }
    if(src->height != dst->height || src->width != dst->width)
    {
        printf("src - blobImage sizes dont match!\n");
        return;
    }
    lutMutex->lock();
    for (int i = 0; i < src->height; ++i)
    {
        for (int j = 0; j < src->width; ++j)
        {
            uchar *pin = &IMGDATA(src, i, j, 0);
            int b = *pin;
            int g = *(pin+1);
            int r = *(pin+2);
            IMGDATA(dst, i, j, 0) = lut->lookup(b, g, r);
        }
    }
    lutMutex->unlock();
}

void CameraWorker::drawBlobs(IplImage *dst, std::vector<BlobData> bA)
{
    if(!dst)
    {
        qDebug() << "draw blobs has empty image";
        return;
    }
    lutMutex->lock();
    for(int i=0; i<bA.size(); i++)
    {
        cvRectangle(dst, convertToCalibCoord(bA[i].p1), convertToCalibCoord(bA[i].p2), lut->getScalar(bA[i].color));
    }
    lutMutex->unlock();
}

void CameraWorker::onTrainClicked(int shapeType, QString name)
{
    if(isPrimaryShape)
    {
        qDebug() << "trained primary!\n";
        matcher->train(primaryDescriptor, (ShapeType)shapeType, name.toStdString());
    }
    else
    {
        qDebug() << "trained secondayr!";
        matcher->train(secondaryDescriptor, (ShapeType)shapeType, name.toStdString());
    }
    qDebug() << "Train set size = " << matcher->getTrainedSetSize();
    emit shapeListChanged(matcher->getTrainingSet());
}

void CameraWorker::onDeleteAllShape()
{
    while(matcher->getTrainedSetSize())
        matcher->deleteShape(0);
    emit shapeListChanged(matcher->getTrainingSet());
}

void CameraWorker::onDeleteShape(int idx)
{
    matcher->deleteShape(idx);
    emit shapeListChanged(matcher->getTrainingSet());
}

void CameraWorker::onPrimaryRadioToggled(bool checked)
{
    isPrimaryShape = checked;
//    qDebug() << "primary toggled!";
}
