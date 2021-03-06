#include "mainwindow.h"
#include "set.h"
#include "ui_mainwindow.h"

#include <QMenu>
#include <QImage>
#include <QKeySequence>
#include <QToolBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QDesktopWidget>
#include <QVector>
#include <QFont>

const HV_RESOLUTION Resolution = RES_MODE0;
const HV_SNAP_MODE SnapMode = CONTINUATION;
const HV_BAYER_CONVERT_TYPE ConvertType = BAYER2RGB_NEIGHBOUR1;
const HV_SNAP_SPEED SnapSpeed = HIGH_SPEED;
long ADCLevel           = ADC_LEVEL2;
const int XStart              = 0;//图像左上角点在相机幅面1280X1024上相对于幅面左上角点坐标
const int YStart              = 0;
int scanWidth;//扫描区域
int scanHeight;
bool inEnglish = true;
int nowProgress = 0;//进度条初始化

QFont textf("Calibri",50);
QColor greencolor(0,255,0);
QColor orangecolor(238,76,0);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow)
{
    /*****生成主窗口UI*****/
    ui->setupUi(this);

    /*****声明全局变量*****/
    saveCount = 1;//Save calib images start with 1
    scanSquenceNo = -1;
    cameraOpened = false;
    isConfigured = false;
    isProjectorOpened = true;

    /****生成计时器并连接*****/
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(readframe()));

    /*****生成OpenGL窗口并加载*****/
    displayModel = new GLWidget(ui->displayWidget);
    ui->displayLayout->addWidget(displayModel);

    /*****生成设置窗口并输出默认设置*****/
    setDialog = new Set(this);//Initialize the set dialog
    getSetInfo();

    /*****获取屏幕尺寸信息*****/
    getScreenGeometry();//Get mian screen and projector screen geometry
    QDesktopWidget* desktopWidget = QApplication::desktop();
    QRect projRect = desktopWidget->screenGeometry(1);//1 represent projector
    int xOffSet = (projRect.width() - scanWidth)/2 + screenWidth;
    int yOffSet = (projRect.height() - scanHeight)/2;

    /*****初始化投影窗口*****/
    pj = new Projector(NULL, scanWidth, scanHeight, projectorWidth, projectorHeight, xOffSet, yOffSet);//Initialize the projector
    pj->move(screenWidth,0);//make the window displayed by the projector
    pj->showFullScreen();

    /*****建立连接*****/
    createConnections();

    /*****初始化圆点探测*****/
    blob = new BlobDetector();
}

MainWindow::~MainWindow()
{
    if(cameraOpened){
        OnSnapexStop();
        OnSnapexClose();
        HVSTATUS status = STATUS_OK;
        //	关闭数字摄像机，释放数字摄像机内部资源
        status = EndHVDevice(m_hhv_1);
        status = EndHVDevice(m_hhv_2);
        //	回收图像缓冲区
        delete []m_pRawBuffer_1;
        delete []m_pRawBuffer_2;
    }
    delete pj;
    delete blob;
    delete ui;
}

void MainWindow::newproject()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"),"/home",QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    projectPath = dir;
    if(projectPath != "")
    {
        for(int i = 0;i<3;i++){
            generatePath(i);
        }
        dm = new DotMatch(this, projectPath);
    }
}

void MainWindow::openproject()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"),"/home",QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    projectPath = dir;
}

///---------------------相机-----------------------///
void MainWindow::exposurecontrol()
{
    switch (ui->leftExSlider->value()) {
    case 0:
        ADCLevel = ADC_LEVEL3;
        break;
    case 1:
        ADCLevel = ADC_LEVEL2;
        break;
    case 2:
        ADCLevel = ADC_LEVEL1;
        break;
    case 3:
        ADCLevel = ADC_LEVEL0;
        break;
    }
    HVADCControl(m_hhv_1, ADC_BITS, ADCLevel);
    switch (ui->rightExSlider->value()) {
    case 0:
        ADCLevel = ADC_LEVEL3;
        break;
    case 1:
        ADCLevel = ADC_LEVEL2;
        break;
    case 2:
        ADCLevel = ADC_LEVEL1;
        break;
    case 3:
        ADCLevel = ADC_LEVEL0;
        break;
    }
    HVADCControl(m_hhv_2, ADC_BITS, ADCLevel);
}

void MainWindow::opencamera()
{
    HVSTATUS status_1 = STATUS_OK;
    HVSTATUS status_2 = STATUS_OK;
    m_pRawBuffer_1	= NULL;
    m_pRawBuffer_2	= NULL;

    status_1 = BeginHVDevice(1, &m_hhv_1);
    status_2 = BeginHVDevice(2, &m_hhv_2);
    if(status_1==STATUS_OK&&status_2==STATUS_OK)
        cameraOpened = true;
    else{
        cameraOpened = false;
        QMessageBox::warning(NULL, tr("Cameras not found"), tr("Make sure two Daheng cameras have connected to the computer."));
        return;
    }
    HVSetResolution(m_hhv_1, Resolution);//Set the resolution of cameras
    HVSetResolution(m_hhv_2, Resolution);

    HVSetSnapMode(m_hhv_1, SnapMode);//Snap mode include CONTINUATION、TRIGGER
    HVSetSnapMode(m_hhv_2, SnapMode);

    HVADCControl(m_hhv_1, ADC_BITS, ADCLevel);//设置ADC的级别
    HVADCControl(m_hhv_2, ADC_BITS, ADCLevel);

    HVTYPE type = UNKNOWN_TYPE;//获取设备类型
    int size    = sizeof(HVTYPE);
    HVGetDeviceInfo(m_hhv_1,DESC_DEVICE_TYPE, &type, &size);//由于两相机型号相同，故只获取一个

    HVSetOutputWindow(m_hhv_1, XStart, YStart, cameraWidth, cameraHeight);
    HVSetOutputWindow(m_hhv_2, XStart, YStart, cameraWidth, cameraHeight);

    HVSetSnapSpeed(m_hhv_1, SnapSpeed);//设置采集速度
    HVSetSnapSpeed(m_hhv_2, SnapSpeed);

    m_pRawBuffer_1 = new BYTE[cameraWidth * cameraHeight];
    m_pRawBuffer_2 = new BYTE[cameraWidth * cameraHeight];

    OnSnapexOpen();
    OnSnapexStart();
    timer->start(30);

    ui->actionOpenCamera->setDisabled(true);//暂时保证不会启动两次，防止内存溢出
    ui->leftExSlider->setEnabled(true);//激活曝光调整滑块
    ui->rightExSlider->setEnabled(true);
}

void MainWindow::OnSnapexOpen()
{
    HVSTATUS status = STATUS_OK;
    status = HVOpenSnap(m_hhv_1, SnapThreadCallback, this);
    status = HVOpenSnap(m_hhv_2, SnapThreadCallback, this);
}

void MainWindow::OnSnapexStart()
{
    HVSTATUS status = STATUS_OK;
    ppBuf_1[0] = m_pRawBuffer_1;
    ppBuf_2[0] = m_pRawBuffer_2;
    status = HVStartSnap(m_hhv_1, ppBuf_1,1);
    status = HVStartSnap(m_hhv_2, ppBuf_2,1);
}

void MainWindow::OnSnapexStop()
{
    HVSTATUS status = STATUS_OK;
    status = HVStopSnap(m_hhv_1);
    status = HVStopSnap(m_hhv_2);
}

void MainWindow::OnSnapexClose()
{
    HVSTATUS status = STATUS_OK;
    status = HVCloseSnap(m_hhv_1);
    status = HVCloseSnap(m_hhv_2);
}

void MainWindow::closeCamera()
{
    timer->stop();
    OnSnapexStop();
    OnSnapexClose();
}

int CALLBACK MainWindow::SnapThreadCallback(HV_SNAP_INFO *pInfo)
{
    return 1;
}

void MainWindow::readframe()
{
    image_1 = QImage(m_pRawBuffer_1, cameraWidth, cameraHeight, QImage::Format_Indexed8);
    image_2 = QImage(m_pRawBuffer_2, cameraWidth, cameraHeight, QImage::Format_Indexed8);
    pimage_1 = QPixmap::fromImage(image_1);
    pimage_2 = QPixmap::fromImage(image_2);
    ui->leftViewLabel->setPixmap(pimage_1);
    ui->rightViewLabel->setPixmap(pimage_2);
}

///-------------------标定-------------------///
void MainWindow::calib()
{
    QMessageBox::information(NULL, tr("Calibration"), tr("Calibration Actived!"));
    selectPath(PATHCALIB);
    ui->tabWidget->setCurrentIndex(0);//go to calibration page
    ui->explainLabel->setPixmap(":/" + QString::number(saveCount) + ".png");
    ui->calibButton->setEnabled(true);
}


void MainWindow::capturecalib()
{
    if(cameraOpened){
        captureImage("", saveCount, true);
        ui->currentPhotoLabel->setText(QString::number(saveCount));
        saveCount++;
        QString explain = ":/" + QString::number(saveCount) + ".png";
        ui->explainLabel->setPixmap(explain);
        if(saveCount == 13){
            saveCount = 1;
            ui->calibButton->setEnabled(true);
        }
    }
    else
        QMessageBox::warning(this, tr("Warning"), tr("Open cameras failed."));
}


void MainWindow::redocapture()
{
    if(cameraOpened){
        captureImage("", saveCount - 1, true);
        }
    else
        QMessageBox::warning(this,tr("Warning"), tr("Open cameras failed."));
}


void MainWindow::captureImage(QString pref, int saveCount,bool dispaly)
{
    pimage_1.save(projChildPath + "/left/" + pref + "L" + QString::number(saveCount) +".png");
    pimage_2.save(projChildPath + "/right/" + pref + "R" + QString::number(saveCount) +".png");
    if(dispaly){
        for (size_t camCount = 0; camCount < 2; camCount++){
                BYTE *buffer = (camCount == 0)?(image_1.bits()):(image_2.bits());
                cv::Mat mat = cv::Mat(cameraHeight, cameraWidth, CV_8UC1, buffer);//直接从内存缓冲区获得图像数据是可行的
                //int bwThreshold = dm->OSTU_Region(mat);
                cv::Mat bimage = mat >= 60;
                cv::bitwise_not(bimage, bimage);
                vector<cv::Point2d> centers;
                blob->findBlobs(bimage,centers);
            if(camCount==0){
                QPixmap pcopy = pimage_1;
                QPainter pt(&pcopy);
                pt.setPen(greencolor);
                for (size_t i = 0; i < centers.size();i++)
                {
                    drawCross(pt,centers[i].x,centers[i].y);
                }
                ui->leftCaptureLabel->setPixmap(pcopy);
            }
            else{
                QPixmap pcopy_1 = pimage_2;
                QPainter pt_1(&pcopy_1);
                pt_1.setPen(greencolor);
                for (size_t i = 0; i < centers.size();i++)
                {
                    drawCross(pt_1,centers[i].x,centers[i].y);
                }
                ui->rightCaptureLabel->setPixmap(pcopy_1);
            }
        }
    }
}


void MainWindow::calibration()
{
    ui->progressBar->reset();
    nowProgress = 0;
    calibrator = new CameraCalibration();
    calibrator->setSquareSize(cvSize(setDialog->cell_w,setDialog->cell_h));
    QString path;

    for(int i = 1; i <= 2; i++)
    {
        path = projectPath + "/calib/";

        if (i == 1){
            path += "left/L";
            calibrator->isleft = true;
        }
        else{
            path += "right/R";
            calibrator->isleft = false;
        }

        //load images
        calibrator->loadCameraImgs(path);
        progressPop(5);
        calibrator->extractImageCorners();
        progressPop(15);
        calibrator->calibrateCamera();
        (i==1)?(ui->leftRMS->setText(QString::number(calibrator->rms))):(ui->rightRMS->setText(QString::number(calibrator->rms)));
        progressPop(10);
        calibrator->findCameraExtrisics();
        progressPop(10);

        //export txt files
        QString file_name;
        path = (i==1)?(projectPath + "/calib/left/"):(projectPath + "/calib/right/");

        file_name =  path;
        file_name += "cam_matrix.txt";
        calibrator->exportTxtFiles(file_name.toLocal8Bit(),CAMCALIB_OUT_MATRIX);

        file_name =  path;
        file_name += "cam_distortion.txt";
        calibrator->exportTxtFiles(file_name.toLocal8Bit(),CAMCALIB_OUT_DISTORTION);

        file_name =  path;
        file_name += "cam_rotation_matrix.txt";
        calibrator->exportTxtFiles(file_name.toLocal8Bit(),CAMCALIB_OUT_ROTATION);

        file_name =  path;
        file_name += "cam_trans_vectror.txt";
        calibrator->exportTxtFiles(file_name.toLocal8Bit(),CAMCALIB_OUT_TRANSLATION);
        progressPop(10);
    }
    path = projectPath + "/calib/fundamental_mat.txt";
    calibrator->findFundamental();
    calibrator->exportTxtFiles(path.toLocal8Bit(), CAMCALIB_OUT_FUNDAMENTAL);
    path = projectPath + "/calib/H1_mat.txt";
    calibrator->exportTxtFiles(path.toLocal8Bit(), CAMCALIB_OUT_H1);
    path = projectPath + "/calib/H2_mat.txt";
    calibrator->exportTxtFiles(path.toLocal8Bit(), CAMCALIB_OUT_H2);
    path = projectPath + "/calib/status_mat.txt";
    calibrator->exportTxtFiles(path.toLocal8Bit(), CAMCALIB_OUT_STATUS);
    ui->progressBar->setValue(100);
}

///-----------------------扫描---------------------------///
void MainWindow::scan()
{
    ui->progressBar->reset();
    nowProgress = 0;

    if(!cameraOpened){
        QMessageBox::warning(this, tr("Cameras are not Opened"), tr("Cameras are not opened."));
        return;
    }
    if(projectPath==""){
        QMessageBox::warning(this,tr("Save Path hasn't been Set"), tr("You need create a project first."));
        return;
    }
    selectPath(PATHSCAN);
    ui->tabWidget->setCurrentIndex(PATHSCAN);
    ui->findPointButton->setEnabled(true);
    ui->reFindButton->setEnabled(true);
    ui->startScanButton->setEnabled(true);
    pj->setCrossVisable(false);
}

void MainWindow::pointmatch()
{
    findPoint();
}

void MainWindow::refindmatch()
{
    dm->scanNo--;
    findPoint();
}

void MainWindow::findPoint()
{
    if (dm->dotForMark.size() != 0)
    {
        dm->dotForMark.clear();
    }
    cv::Mat mat_1 = cv::Mat(cameraHeight, cameraWidth, CV_8UC1, m_pRawBuffer_1);//直接从内存缓冲区获得图像数据是可行的
    cv::Mat mat_2 = cv::Mat(cameraHeight, cameraWidth, CV_8UC1, m_pRawBuffer_2);
    //imshow("d",mat_1);
    //cvWaitKey(10);
    scanSquenceNo = dm->scanNo;
    ui->scanNoLabel->setText(QString::number(scanSquenceNo));
    dm->matchDot(mat_1,mat_2);
    QPixmap pcopy_1 = pimage_1;
    QPixmap pcopy_2 = pimage_2;
    QPainter pt_1(&pcopy_1);
    QPainter pt_2(&pcopy_2);
    pt_1.setFont(textf);
    pt_2.setFont(textf);

    for(int i = 0;i < dm->dotForMark.size();i++)
    {
        if (dm->dotForMark[i][4] == 1)
        {
            pt_1.setPen(greencolor);
            pt_2.setPen(greencolor);
            pt_1.drawText(dm->dotForMark[i][0],dm->dotForMark[i][1],QString::number(dm->dotForMark[i][5]));
            pt_2.drawText(dm->dotForMark[i][2],dm->dotForMark[i][3],QString::number(dm->dotForMark[i][5]));
            drawCross(pt_1, dm->dotForMark[i][0] ,dm->dotForMark[i][1]);
            drawCross(pt_2, dm->dotForMark[i][2], dm->dotForMark[i][3]);
        }
        else
        {
            pt_1.setPen(orangecolor);
            pt_2.setPen(orangecolor);
            drawCross(pt_1, dm->dotForMark[i][0] ,dm->dotForMark[i][1]);
            drawCross(pt_2, dm->dotForMark[i][2], dm->dotForMark[i][3]);
        }
    }
    ui->leftCaptureLabel->setPixmap(pcopy_1);
    ui->rightCaptureLabel->setPixmap(pcopy_2);
}

void MainWindow::startscan()
{
    if (scanSquenceNo < 0)
    {
        if (QMessageBox::warning(this,tr("Mark Point Need to be Found"), tr("Scan result can't be aligned,continue?")
                                 ,QMessageBox::Yes,QMessageBox::No) == QMessageBox::No)
            return;
    }
    ui->progressBar->reset();
    nowProgress = 0;

    closeCamera();
    pj->displaySwitch(false);
    pj->opencvWindow();
    if (ui->useGray->isChecked())
    {
        grayCode = new GrayCodes(projectorWidth,projectorHeight);
        grayCode->generateGrays();
        pj->showImg(grayCode->getNextImg());
    }
    else
    {
        mf = new MultiFrequency(this, scanWidth, scanHeight);
        mf->generateMutiFreq();
        pj->showMatImg(mf->getNextMultiFreq());
    }
    progressPop(10);

    int imgCount = 0;

    QString pref = QString::number(scanSquenceNo) + "/";
    QDir *addpath_1 = new QDir;
    QDir *addpath_2 = new QDir;
    addpath_1->mkpath(projChildPath + "/left/" + pref);
    addpath_2->mkpath(projChildPath +"/right/" + pref);

    while(true)
    {
        cvWaitKey(100);
        HVSnapShot(m_hhv_1, ppBuf_1, 1);
        image_1 = QImage(m_pRawBuffer_1, cameraWidth, cameraHeight, QImage::Format_Indexed8);
        pimage_1 = QPixmap::fromImage(image_1);

        HVSnapShot(m_hhv_2, ppBuf_2, 1);
        image_2 = QImage(m_pRawBuffer_2, cameraWidth, cameraHeight, QImage::Format_Indexed8);
        pimage_2 = QPixmap::fromImage(image_2);

        captureImage(pref, imgCount, false);
        imgCount++;
        //show captured result
        if (ui->useGray->isChecked())
        {
            if(imgCount == grayCode->getNumOfImgs())
                break;
            pj->showImg(grayCode->getNextImg());
            progressPop(2);
        }
        else
        {
            if(imgCount == mf->getNumOfImgs())
                break;
            pj->showMatImg(mf->getNextMultiFreq());
            progressPop(7);
        }
    }
    HVOpenSnap(m_hhv_1,SnapThreadCallback, this);
    HVOpenSnap(m_hhv_2,SnapThreadCallback, this);
    HVStartSnap(m_hhv_1,ppBuf_1,1);
    HVStartSnap(m_hhv_2,ppBuf_2,1);
    timer->start();
    pj->destoryWindow();
    pj->displaySwitch(true);

    ui->progressBar->setValue(100);
}

void MainWindow::testmulitfreq()
{
    pj->displaySwitch(false);
    pj->opencvWindow();
    MultiFrequency *mf = new MultiFrequency(this, scanWidth, scanHeight);
    mf->generateMutiFreq();
    cv::Mat play = mf->getNextMultiFreq();
    pj->showMatImg(play);
}

///-----------------重建-------------------///
void MainWindow::reconstruct()
{
    ui->progressBar->reset();
    nowProgress = 0;
    ui->tabWidget->setCurrentIndex(2);
    selectPath(PATHRECON);//set current path to :/reconstruct
}

void MainWindow::startreconstruct()
{
    ui->progressBar->reset();
    nowProgress = 0;
    if(cameraOpened)
        closeCamera();
    if(isConfigured == false){
        if(QMessageBox::warning(this,tr("Warning"), tr("You may want to change the settings, continue with default settings?"),
                QMessageBox::Yes,QMessageBox::No) == QMessageBox::No)
            return;
        else
            isConfigured = true;
    }

    Reconstruct *reconstructor= new Reconstruct();
    int scanCount = (ui->manualReconstruction->isChecked())?(ui->reconstructionCount->value()):(scanSquenceNo);
    reconstructor->getParameters(scanWidth, scanHeight, cameraWidth, cameraHeight, scanCount, isAutoContrast, projectPath);

    reconstructor->setCalibPath(projectPath+"/calib/left/", 0);
    reconstructor->setCalibPath(projectPath+"/calib/right/", 1);
    bool loaded = reconstructor->loadCameras();//load camera matrix
    if(!loaded)
    {
        ui->progressBar->reset();
        nowProgress = 0;
        return;
    }
    progressPop(5);

    reconstructor->setBlackThreshold(black_);
    reconstructor->setWhiteThreshold(white_);

    if(isRaySampling)
        reconstructor->enableRaySampling();
    else
        reconstructor->disableRaySampling();
    progressPop(15);

    bool runSucess = reconstructor->runReconstruction();
    if(!runSucess)
    {
        ui->progressBar->reset();
        nowProgress = 0;
        return;
    }
    progressPop(50);

    MeshCreator *meshCreator=new MeshCreator(reconstructor->points3DProjView);//Export mesh
    progressPop(10);

    if(isExportObj)
        meshCreator->exportObjMesh(projChildPath + QString::number(scanSquenceNo) + ".obj");
    if(isExportPly || !(isExportObj || isExportPly))
    {
        QString outPlyPath = projChildPath + QString::number(scanSquenceNo) + ".ply";
        meshCreator->exportPlyMesh(outPlyPath);
        displayModel->LoadModel(outPlyPath);
    }
    delete meshCreator;
    delete reconstructor;
    opencamera();
    ui->progressBar->setValue(100);
}

void MainWindow::set()
{
    setDialog->show();
    setDialog->saveSetPath = projectPath;
    isConfigured = true;
    connect(setDialog,SIGNAL(outputSet()),this,SLOT(getSetInfo()));
}


void MainWindow::getSetInfo()
{
    cameraWidth = setDialog->cam_w;
    cameraHeight = setDialog->cam_h;
    projectorWidth = setDialog->proj_w;
    projectorHeight = setDialog->proj_h;
    scanWidth = setDialog->scan_w;
    scanHeight = setDialog->scan_h;
    black_ = setDialog->black_threshold;
    white_ = setDialog->white_threshold;
    isAutoContrast = setDialog->autoContrast;
    isRaySampling = setDialog->raySampling;
    isExportObj = setDialog->exportObj;
    isExportPly = setDialog->exportPly;
}


void MainWindow::createConnections()
{
    connect(ui->actionNew, SIGNAL(triggered()), this, SLOT(newproject()));
    connect(ui->actionOpen, SIGNAL(triggered()), this, SLOT(openproject()));

    connect(ui->actionOpenCamera, SIGNAL(triggered()), this, SLOT(opencamera()));
    connect(ui->leftExSlider,SIGNAL(valueChanged(int)),this,SLOT(exposurecontrol()));
    connect(ui->rightExSlider,SIGNAL(valueChanged(int)),this,SLOT(exposurecontrol()));

    connect(ui->actionProjector,SIGNAL(triggered()),this,SLOT(projectorcontrol()));

    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(selectPath(int)));

    connect(ui->actionCalib, SIGNAL(triggered()), this, SLOT(calib()));
    connect(ui->captureButton, SIGNAL(clicked()), this, SLOT(capturecalib()));
    connect(ui->redoButton, SIGNAL(clicked()), this, SLOT(redocapture()));
    connect(ui->calibButton,SIGNAL(clicked()),this,SLOT(calibration()));

    connect(ui->actionScan,SIGNAL(triggered()),this,SLOT(scan()));
    connect(ui->findPointButton,SIGNAL(clicked()),this,SLOT(pointmatch()));
    connect(ui->reFindButton,SIGNAL(clicked()),this,SLOT(refindmatch()));
    connect(ui->startScanButton, SIGNAL(clicked()), this, SLOT(startscan()));
    connect(ui->multiFreqTest, SIGNAL(clicked()), this, SLOT(testmulitfreq()));

    connect(ui->actionReconstruct,SIGNAL(triggered()),this,SLOT(reconstruct()));
    connect(ui->reconstructionButton,SIGNAL(clicked()),this,SLOT(startreconstruct()));

    connect(ui->actionSet, SIGNAL(triggered()), this, SLOT(set()));
    connect(ui->actionChinese, SIGNAL(triggered()), this, SLOT(switchlanguage()));
    connect(ui->actionEnglish, SIGNAL(triggered()), this, SLOT(switchlanguage()));
    connect(ui->pSizeValue, SIGNAL(valueChanged(int)), this, SLOT(changePointSize(int)));
    connect(ui->loadTest, SIGNAL(clicked()), this, SLOT(loadTestModel()));

    connect(ui->actionExit, SIGNAL(triggered()), pj, SLOT(close()));//解决投影窗口不能关掉的问题
    connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(close()));
}

///------------------辅助功能-------------------///

void MainWindow::generatePath(int type)
{
    selectPath(type);
    QDir *addpath_1 = new QDir;
    QDir *addpath_2 = new QDir;
    if(type == 0 || type == 1)
    {
        addpath_1->mkpath(projChildPath + "/left/");
        addpath_2->mkpath(projChildPath +"/right/");
    }
    else
        addpath_1->mkpath(projChildPath);
}

void MainWindow::selectPath(int PATH)//decide current childpath
{
    QString t;
    switch (PATH) {
        case PATHCALIB:
        t = "/calib/";
        break;
        case PATHSCAN:
        t = "/scan/";
        break;
        case PATHRECON:
        t = "/reconstruction/";
        break;
    }
    projChildPath = projectPath + t;
}

void MainWindow::projectorcontrol()
{
    isProjectorOpened = !isProjectorOpened;
    if(isProjectorOpened){
        pj->displaySwitch(true);
    }
    else{
        pj->displaySwitch(false);
    }
}

void MainWindow::getScreenGeometry()
{
    QDesktopWidget* desktopWidget = QApplication::desktop();
    int screencount = desktopWidget->screenCount();//get screen amount
    if(screencount == 1){
    }
    else{
        QRect screenRect = desktopWidget->screenGeometry(0);
        screenWidth = screenRect.width();
        screenHeight = screenRect.height();
    }
}

void MainWindow::switchlanguage()
{
    QString qmPath = ":/";//表示从资源文件夹中加载
    QString local;
    if(inEnglish)
    {
        local = "zh.pm";
        inEnglish = false;
        ui->actionEnglish->setEnabled(true);
        ui->actionChinese->setDisabled(true);
    }
    else
    {
        local = "en.pm";
        inEnglish = true;
        ui->actionEnglish->setEnabled(false);
        ui->actionChinese->setDisabled(false);
    }
    QTranslator trans;
    trans.load(local, qmPath);
    qApp->installTranslator(&trans);
    ui->retranslateUi(this);
    setDialog->switchLang();
}

void MainWindow::changePointSize(int psize)
{
    displayModel->setPoint(psize);
}

void MainWindow::loadTestModel()
{
    if (projChildPath != NULL)
    {
        QString testPath = projChildPath + "0.ply";
        displayModel->LoadModel(testPath);
    }
    else
    {
        QMessageBox::warning(NULL,tr("File Not Found"),tr("Test file doesn't exist."));
        return;
    }
}


void MainWindow::progressPop(int up)
{
    nowProgress += up;
    ui->progressBar->setValue(nowProgress);
}

void MainWindow::drawCross(QPainter &p, int x, int y)
{
    int len = 25;
    p.drawLine(x - len, y, x + len, y);
    p.drawLine(x, y - len, x, y + len);
}
