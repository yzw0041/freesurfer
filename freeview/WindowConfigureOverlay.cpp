/**
 * @file  WindowConfigureOverlay.cpp
 * @brief REPLACE_WITH_ONE_LINE_SHORT_DESCRIPTION
 *
 */
/*
 * Original Author: Ruopeng Wang
 * CVS Revision Info:
 *    $Author: rpwang $
 *    $Date: 2017/02/01 15:28:54 $
 *    $Revision: 1.21 $
 *
 * Copyright © 2011 The General Hospital Corporation (Boston, MA) "MGH"
 *
 * Terms and conditions for use, reproduction, distribution and contribution
 * are found in the 'FreeSurfer Software License Agreement' contained
 * in the file 'LICENSE' found in the FreeSurfer distribution, and here:
 *
 * https://surfer.nmr.mgh.harvard.edu/fswiki/FreeSurferSoftwareLicense
 *
 * Reporting: freesurfer@nmr.mgh.harvard.edu
 *
 */
#include "WindowConfigureOverlay.h"
#include "ui_WindowConfigureOverlay.h"
#include "LayerSurface.h"
#include "SurfaceOverlayProperty.h"
#include "SurfaceOverlay.h"
#include "LayerPropertySurface.h"
#include "MainWindow.h"
#include "LayerCollection.h"
#include "LayerMRI.h"
#include <QMessageBox>
#include <QDebug>
#include <QSettings>

WindowConfigureOverlay::WindowConfigureOverlay(QWidget *parent) :
  QWidget(parent), UIUpdateHelper(),
  ui(new Ui::WindowConfigureOverlay),
  m_dSavedOffset(0)
{
  ui->setupUi(this);
  setWindowFlags( Qt::Tool );
  m_fDataCache = NULL;
  ui->widgetHistogram->SetNumberOfBins( 200 );
  ui->widgetHolderAddPoint->hide();
  ui->checkBoxClearLower->hide();
  ui->checkBoxClearHigher->hide();
  ui->pushButtonFlip->hide();
  ui->widgetColorPicker->setCurrentColor(Qt::green);
  ui->buttonBox->button(QDialogButtonBox::Apply)->setAutoDefault(true);
  connect(ui->widgetHistogram, SIGNAL(MarkerChanged()), this, SLOT(OnHistogramMarkerChanged()));
  connect(ui->checkBoxApplyToAll, SIGNAL(toggled(bool)), this, SLOT(OnCheckApplyToAll(bool)));
  m_layerSurface = NULL;
  QSettings settings;
  QVariant v = settings.value("WindowConfigureOverlay/Geometry");
  if (v.isValid())
  {
    this->restoreGeometry(v.toByteArray());
  }
  v = settings.value("WindowConfigureOverlay/AutoApply");
  if (!v.isValid())
    v = true;
  ui->checkBoxAutoApply->setChecked(v.toBool());
  ui->checkBoxAutoFrame->setChecked(settings.value("WindowConfigureOverlay/AutoFrame").toBool());

  LayerCollection* lc = MainWindow::GetMainWindow()->GetLayerCollection("MRI");
  connect(lc, SIGNAL(LayerAdded(Layer*)), this, SLOT(UpdateUI()));
  connect(lc, SIGNAL(LayerRemoved(Layer*)), this, SLOT(UpdateUI()));
}

WindowConfigureOverlay::~WindowConfigureOverlay()
{
  if (m_fDataCache)
    delete[] m_fDataCache;
  m_fDataCache = 0;

  QSettings settings;
  settings.setValue("WindowConfigureOverlay/Geometry", this->saveGeometry());
  settings.setValue("WindowConfigureOverlay/AutoApply", ui->checkBoxAutoApply->isChecked());
  settings.setValue("WindowConfigureOverlay/AutoFrame", ui->checkBoxAutoFrame->isChecked());

  delete ui;
}

void WindowConfigureOverlay::showEvent(QShowEvent *)
{
  UpdateUI();
  UpdateGraph();
}

void WindowConfigureOverlay::OnActiveSurfaceChanged(Layer* layer)
{
  if (m_layerSurface)
  {
    disconnect(m_layerSurface, 0, this, 0);
  }
  m_layerSurface = qobject_cast<LayerSurface*>(layer);
  if (m_layerSurface)
  {
    connect(m_layerSurface, SIGNAL(SurfaceOverlyDataUpdated()),
            this, SLOT(UpdateUI()), Qt::UniqueConnection);
    connect(m_layerSurface, SIGNAL(SurfaceOverlyDataUpdated()),
            this, SLOT(UpdateGraph()), Qt::UniqueConnection);
    connect(m_layerSurface, SIGNAL(ActiveOverlayChanged(int)),
            this, SLOT(UpdateUI()), Qt::UniqueConnection);
    connect(m_layerSurface, SIGNAL(ActiveOverlayChanged(int)),
            this, SLOT(UpdateGraph()), Qt::UniqueConnection);
  }

  if (m_fDataCache)
    delete[] m_fDataCache;
  m_fDataCache = 0;

  UpdateUI();
  UpdateGraph();
}

void WindowConfigureOverlay::UpdateUI()
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    QList<QWidget*> allwidgets = this->findChildren<QWidget*>();
    for ( int i = 0; i < allwidgets.size(); i++ )
    {
      allwidgets[i]->blockSignals( true );
    }
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    SurfaceOverlayProperty* p = overlay->GetProperty();
    ui->sliderOpacity->setValue( (int)( p->GetOpacity() * 100 ) );
    ChangeDoubleSpinBoxValue( ui->doubleSpinBoxOpacity, p->GetOpacity() );

    ui->sliderFrame->setRange(0, overlay->GetNumberOfFrames()-1);
    ui->sliderFrame->setValue(overlay->GetActiveFrame());
    ui->spinBoxFrame->setRange(0, overlay->GetNumberOfFrames()-1);
    ui->spinBoxFrame->setValue(overlay->GetActiveFrame());
    ui->labelFrameRange->setText(QString("0-%1").arg(overlay->GetNumberOfFrames()-1));
    ui->groupBoxFrame->setVisible(overlay->GetNumberOfFrames() > 1);

    //   ui->radioButtonGreenRed ->setChecked( p->GetColorScale() == SurfaceOverlayProperty::CS_GreenRed );
    //   ui->radioButtonBlueRed    ->setChecked( p->GetColorScale() == SurfaceOverlayProperty::CS_BlueRed );
    ui->radioButtonHeat       ->setChecked( p->GetColorScale() == SurfaceOverlayProperty::CS_Heat );
    ui->radioButtonColorWheel ->setChecked( p->GetColorScale() == SurfaceOverlayProperty::CS_ColorWheel );
    ui->radioButtonCustom  ->setChecked( p->GetColorScale() == SurfaceOverlayProperty::CS_Custom );

    ui->checkBoxUsePercentile->setChecked(p->GetUsePercentile());
    ui->widgetHistogram->SetUsePercentile(p->GetUsePercentile());

    if (ui->checkBoxUsePercentile->isChecked())
    {
      ChangeLineEditNumber( ui->lineEditMin, ui->widgetHistogram->PositionToPercentile(p->GetMinPoint()) );
      ChangeLineEditNumber( ui->lineEditMid, ui->widgetHistogram->PositionToPercentile(p->GetMidPoint()) );
      ChangeLineEditNumber( ui->lineEditMax, ui->widgetHistogram->PositionToPercentile(p->GetMaxPoint()) );
    }
    else
    {
      ChangeLineEditNumber( ui->lineEditMin, p->GetMinPoint() );
      ChangeLineEditNumber( ui->lineEditMid, p->GetMidPoint() );
      ChangeLineEditNumber( ui->lineEditMax, p->GetMaxPoint() );
    }
    ChangeLineEditNumber( ui->lineEditOffset, p->GetOffset());
    m_dSavedOffset = p->GetOffset();

    ui->radioButtonLinear        ->setChecked( p->GetColorMethod() == SurfaceOverlayProperty::CM_Linear );
    ui->radioButtonLinearOpaque  ->setChecked( p->GetColorMethod() == SurfaceOverlayProperty::CM_LinearOpaque );
    ui->radioButtonPiecewise     ->setChecked( p->GetColorMethod() == SurfaceOverlayProperty::CM_Piecewise );

    ui->checkBoxInverse->setChecked( p->GetColorInverse() );
    ui->checkBoxTruncate->setChecked( p->GetColorTruncate() );
    ui->checkBoxClearLower->setChecked(p->GetClearLower());
    ui->checkBoxClearHigher->setChecked(p->GetClearHigher());

    ui->labelMid->setEnabled( ui->radioButtonPiecewise->isChecked() );
    ui->lineEditMid->setEnabled( ui->radioButtonPiecewise->isChecked() );

    ui->checkBoxEnableSmooth->setChecked(p->GetSmooth());
    ui->spinBoxSmoothSteps->setValue(p->GetSmoothSteps());
    ui->spinBoxSmoothSteps->setEnabled(p->GetSmooth());

    QGradientStops stops = p->GetCustomColorScale();
    m_markers.clear();
    for (int i = 0; i < stops.size(); i++)
    {
      LineMarker m;
      m.position = stops[i].first;
      m.color = stops[i].second;
      m_markers << m;
    }

    int nFrames = overlay->GetNumberOfFrames();
    if (nFrames == 1)
      ui->groupBoxFrame->setEnabled(true);
    ui->checkBoxComputeCorrelation->setChecked(overlay->GetComputeCorrelation());

    ui->comboBoxVolumes->clear();
    ui->comboBoxVolumes->addItem("Self");
    if (!overlay->GetCorrelationSourceVolume())
      ui->comboBoxVolumes->setCurrentIndex(0);
    QList<Layer*> layers = MainWindow::GetMainWindow()->GetLayers("MRI");
    foreach (Layer* mri, layers)
    {
      if ((qobject_cast<LayerMRI*>(mri))->GetNumberOfFrames() == nFrames)
      {
        ui->comboBoxVolumes->addItem(mri->GetName(), QVariant::fromValue((QObject*)mri));
        if (overlay->GetCorrelationSourceVolume() == mri)
        {
          ui->comboBoxVolumes->setCurrentIndex(ui->comboBoxVolumes->count()-1);
        }
      }
    }

    ui->checkBoxComputeCorrelation->setEnabled(ui->comboBoxVolumes->count() > 0);
    ui->groupBoxCorrelation->setVisible(nFrames > 1 && ui->comboBoxVolumes->count() > 0);

    for ( int i = 0; i < allwidgets.size(); i++ )
    {
      allwidgets[i]->blockSignals( false );
    }
  }
  else
  {
    close();
  }
}

void WindowConfigureOverlay::OnClicked( QAbstractButton* btn )
{
  if (ui->buttonBox->buttonRole(btn) == QDialogButtonBox::HelpRole)
  {
    QMessageBox::information(this, "Help", "Drag the handle to move point.\n\nAt Custom mode:\nDouble-click on the handle to change point color.\nShift+Click on the handle to remove point.");
  }
  else if (ui->buttonBox->buttonRole(btn) == QDialogButtonBox::ApplyRole)
  {
    /*   if (m_fDataCache)
      delete[] m_fDataCache;
    m_fDataCache = 0;
    */
    OnApply();
  }
}

void WindowConfigureOverlay::OnApply()
{
  if ( !m_layerSurface || !m_layerSurface->GetActiveOverlay() )
  {
    return;
  }

  SurfaceOverlayProperty* p = m_layerSurface->GetActiveOverlay()->GetProperty();
  bool smooth_changed = (p->GetSmooth() != ui->checkBoxEnableSmooth->isChecked() ||
      p->GetSmoothSteps() != ui->spinBoxSmoothSteps->value() );
  if ( UpdateOverlayProperty( p ) )
  {
    if (smooth_changed)
      m_layerSurface->GetActiveOverlay()->UpdateSmooth();
    else
      p->EmitColorMapChanged();
    if (ui->checkBoxApplyToAll->isChecked())
    {
      for (int i = 0; i < m_layerSurface->GetNumberOfOverlays(); i++)
      {
        SurfaceOverlay* so = m_layerSurface->GetOverlay(i);
        if (so != m_layerSurface->GetActiveOverlay())
        {
          so->GetProperty()->Copy(p);
          if (smooth_changed)
            so->UpdateSmooth();
          else
            so->GetProperty()->EmitColorMapChanged();
        }
      }
    }
  }
}

void WindowConfigureOverlay::OnCheckApplyToAll(bool bChecked)
{
  if (bChecked)
  {
    OnApply();
  }
}

bool WindowConfigureOverlay::UpdateOverlayProperty( SurfaceOverlayProperty* p )
{
  p->SetOpacity( ui->doubleSpinBoxOpacity->value() );

  bool bOK;
  double dValue = ui->lineEditMin->text().toDouble(&bOK);

  if ( bOK )
  {
    if (ui->checkBoxUsePercentile->isChecked())
      dValue = ui->widgetHistogram->PercentileToPosition(dValue);
    p->SetMinPoint( dValue );
  }
  else
  {
    return false;
  }

  dValue = ui->lineEditMid->text().toDouble(&bOK);
  if ( bOK )
  {
    if (ui->checkBoxUsePercentile->isChecked())
      dValue = ui->widgetHistogram->PercentileToPosition(dValue);
    p->SetMidPoint( dValue );
  }
  else
  {
    return false;
  }

  dValue = ui->lineEditMax->text().toDouble(&bOK);
  if ( bOK )
  {
    if (ui->checkBoxUsePercentile->isChecked())
      dValue = ui->widgetHistogram->PercentileToPosition(dValue);
    p->SetMaxPoint( dValue );
  }
  else
  {
    return false;
  }

  dValue = ui->lineEditOffset->text().toDouble(&bOK);
  if ( bOK )
  {
    p->SetOffset( dValue );
  }
  else
  {
    return false;
  }

  if ( ui->radioButtonHeat->isChecked() )
  {
    p->SetColorScale( SurfaceOverlayProperty::CS_Heat );
  }
  else if ( ui->radioButtonColorWheel->isChecked() )
  {
    p->SetColorScale( SurfaceOverlayProperty::CS_ColorWheel );
  }
  else if ( ui->radioButtonCustom->isChecked() )
  {
    p->SetColorScale( SurfaceOverlayProperty::CS_Custom );
  }

  if ( ui->radioButtonLinear->isChecked() )
  {
    p->SetColorMethod( SurfaceOverlayProperty::CM_Linear );
  }
  else if ( ui->radioButtonLinearOpaque->isChecked() )
  {
    p->SetColorMethod( SurfaceOverlayProperty::CM_LinearOpaque );
  }
  else if ( ui->radioButtonPiecewise->isChecked() )
  {
    p->SetColorMethod( SurfaceOverlayProperty::CM_Piecewise );
  }

  p->SetColorInverse( ui->checkBoxInverse->isChecked() );
  p->SetColorTruncate( ui->checkBoxTruncate->isChecked() );
  p->SetClearLower(ui->checkBoxClearLower->isChecked());
  p->SetClearHigher(ui->checkBoxClearHigher->isChecked());

  QGradientStops stops;
  for (int i = 0; i < m_markers.size(); i++)
  {
    stops << QGradientStop(m_markers[i].position, m_markers[i].color);
  }
  p->SetCustomColorScale(stops);

  p->SetSmooth(ui->checkBoxEnableSmooth->isChecked());
  p->SetSmoothSteps(ui->spinBoxSmoothSteps->value());

  return true;
}

void WindowConfigureOverlay::UpdateGraph()
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    if ( overlay )
    {
      double range[2];
      overlay->GetRange( range );
      if (range[0] == range[1])
      {
        return;
      }

      SurfaceOverlayProperty* p = new SurfaceOverlayProperty( overlay );
      UpdateOverlayProperty( p );
      if (m_fDataCache)
        ui->widgetHistogram->SetInputData( m_fDataCache, overlay->GetDataSize(), range );
      else
        ui->widgetHistogram->SetInputData( overlay->GetData(), overlay->GetDataSize(), range );
      ui->widgetHistogram->SetSymmetricMarkers(p->GetColorScale() <= SurfaceOverlayProperty::CS_BlueRed);
      ui->widgetHistogram->SetMarkerEditable(p->GetColorScale() == SurfaceOverlayProperty::CS_Custom);

      int nBins = ui->widgetHistogram->GetNumberOfBins();
      float* fData = new float[ nBins ];
      unsigned char* nColorTable = new unsigned char[ nBins*4 ];

      ui->widgetHistogram->GetOutputRange(range);
      double bin_width = ( range[1] - range[0] ) / nBins;
      int rgb[3];
      double* dColor = m_layerSurface->GetProperty()->GetBinaryColor();
      rgb[0] = (int)( dColor[0] * 255 );
      rgb[1] = (int)( dColor[1] * 255 );
      rgb[2] = (int)( dColor[2] * 255 );
      for ( int i = 0; i < nBins; i++ )
      {
        nColorTable[i*4] = rgb[0];
        nColorTable[i*4+1] = rgb[1];
        nColorTable[i*4+2] = rgb[2];
        nColorTable[i*4+3] = 255;

        fData[i] = range[0] + ( i + 0.5 ) * bin_width;
      }
      p->MapOverlayColor( fData, nColorTable, nBins );
      ui->widgetHistogram->SetColorTableData( nColorTable, false );
      delete[] fData;
      delete[] nColorTable;

      LineMarkers markers;
      if (p->GetColorScale() == SurfaceOverlayProperty::CS_Custom)
      {
        markers = m_markers;
      }
      else
      {
        // rebuild marker lines for display
        LineMarker marker;
        marker.position = p->GetMinPoint()+p->GetOffset();
        marker.color = QColor( 255, 0, 0 );
        marker.movable = true;
        markers.push_back( marker );

        if ( p->GetColorMethod() == SurfaceOverlayProperty::CM_Piecewise )
        {
          marker.position = p->GetMidPoint()+p->GetOffset();
          marker.color = QColor( 0, 0, 255 );
          markers.push_back( marker );
        }

        marker.position = p->GetMaxPoint()+p->GetOffset();
        marker.color = QColor( 0, 215, 0 );
        markers.push_back( marker );
      }
      ui->widgetHistogram->SetMarkers( markers );
      delete p;
    }
    if (ui->checkBoxAutoApply->isChecked())
      OnApply();
  }
}

void WindowConfigureOverlay::OnSliderOpacity( int nVal )
{
  ui->doubleSpinBoxOpacity->blockSignals( true );
  ChangeDoubleSpinBoxValue( ui->doubleSpinBoxOpacity, nVal / 100.0 );
  ui->doubleSpinBoxOpacity->blockSignals(false);
  UpdateGraph();
}

void WindowConfigureOverlay::OnSpinBoxOpacity( double dVal )
{
  ui->sliderOpacity->blockSignals(true);
  ui->sliderOpacity->setValue( (int)(dVal * 100) );
  ui->sliderOpacity->blockSignals(false);
  UpdateGraph();
}

void WindowConfigureOverlay::UpdateThresholdChanges()
{
  if ( !ui->radioButtonPiecewise->isChecked() )   // do not adjust mid point automatically in Piecewise mode
  {
    bool bOK;
    double dmin = ui->lineEditMin->text().trimmed().toDouble(&bOK);
    double dmax = ui->lineEditMax->text().trimmed().toDouble(&bOK);

    if ( bOK )
    {
      ui->lineEditMid->blockSignals(true);
      ChangeLineEditNumber(ui->lineEditMid, ( dmax + dmin ) / 2 );
      ui->lineEditMid->blockSignals(false);
    }
  }
  UpdateGraph();
}

void WindowConfigureOverlay::OnHistogramMouseButtonPressed(int button, double value)
{
  value -= m_dSavedOffset;
  if (ui->checkBoxUsePercentile->isChecked())
    value = ui->widgetHistogram->PositionToPercentile(value);

  if (button == Qt::LeftButton)
  {
    ChangeLineEditNumber(ui->lineEditMin, value);
  }
  else if (button == Qt::MidButton)
  {
    ChangeLineEditNumber(ui->lineEditMid, value);
  }
  else if (button == Qt::RightButton)
  {
    ChangeLineEditNumber(ui->lineEditMax, value);
  }
  UpdateThresholdChanges();
}

void WindowConfigureOverlay::OnHistogramMarkerChanged()
{
  LineMarkers markers = ui->widgetHistogram->GetMarkers();
  if (ui->radioButtonCustom->isChecked())
  {
    m_markers = markers;
    UpdateGraph();
  }
  else
  {
    bool bUserPercentile = ui->checkBoxUsePercentile->isChecked();
    for (int i = 0; i < markers.size(); i++)
    {
      if (i == 0)
      {
        if (bUserPercentile)
          ChangeLineEditNumber(ui->lineEditMin, ui->widgetHistogram->PositionToPercentile(markers[i].position-m_dSavedOffset),
                               2, true);
        else
          ChangeLineEditNumber(ui->lineEditMin, markers[i].position-m_dSavedOffset, 2, true);
      }
      if (i == 1)
      {
        if (ui->radioButtonPiecewise->isChecked() && ui->radioButtonHeat->isChecked())
        {
          if (bUserPercentile)
            ChangeLineEditNumber(ui->lineEditMid, ui->widgetHistogram->PositionToPercentile(markers[i].position-m_dSavedOffset),
                                 2, true);
          else
            ChangeLineEditNumber(ui->lineEditMid, markers[i].position-m_dSavedOffset, 2, true);
        }
      }
    }
    if (bUserPercentile)
      ChangeLineEditNumber(ui->lineEditMax, ui->widgetHistogram->PositionToPercentile(markers[markers.size()-1].position-m_dSavedOffset),
          2, true);
    else
      ChangeLineEditNumber(ui->lineEditMax, markers[markers.size()-1].position-m_dSavedOffset, 2, true);
    UpdateThresholdChanges();
  }
}

void WindowConfigureOverlay::OnButtonAdd()
{
  bool bOK;
  double pos = ui->lineEditNewPoint->text().toDouble(&bOK);
  if (!bOK)
  {
    QMessageBox::warning(this, "Error", "Please enter valid new point position.");
    return;
  }
  double range[2];
  ui->widgetHistogram->GetOutputRange(range);
  if (pos < range[0] || pos > range[1])
  {
    QMessageBox::warning(this, "Error", "New point out of range.");
    return;
  }
  ui->widgetHistogram->AddMarker(pos, ui->widgetColorPicker->currentColor());
}

void WindowConfigureOverlay::OnSmoothChanged()
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    if ( overlay )
    {
      if (!m_fDataCache)
        m_fDataCache = new float[overlay->GetDataSize()];

      if (ui->checkBoxEnableSmooth->isChecked())
      {
        overlay->SmoothData(ui->spinBoxSmoothSteps->value(), m_fDataCache);
      }
      else
      {
        memcpy(m_fDataCache, overlay->GetUnsmoothedData(), sizeof(float)*overlay->GetDataSize());
      }
      UpdateGraph();
    }
  }
}

void WindowConfigureOverlay::OnTextThresholdChanged(const QString &strg)
{
  bool ok;
  double val = strg.toDouble(&ok);
  if (!ok)
    return;

  double dOffset = ui->lineEditOffset->text().trimmed().toDouble(&ok);
  if (!ok)
    dOffset = m_dSavedOffset;

  LineMarkers markers = ui->widgetHistogram->GetMarkers();
  if (markers.isEmpty())
    return;

  if (sender() == ui->lineEditMax)
  {
    LineMarker marker = markers.last();
    if (ui->checkBoxUsePercentile->isChecked())
      marker.position = ui->widgetHistogram->PercentileToPosition(val)+dOffset;
    else
      marker.position = val+dOffset;
    markers[markers.size()-1] = marker;
    double val2 = ui->lineEditMin->text().toDouble(&ok);
    if (markers.size() == 2 && ok)
    {
      this->ChangeLineEditNumber(ui->lineEditMid, (val+val2)/2);
    }
  }
  else if (sender() == ui->lineEditMin)
  {
    LineMarker marker = markers.first();
    if (ui->checkBoxUsePercentile->isChecked())
      marker.position = ui->widgetHistogram->PercentileToPosition(val)+dOffset;
    else
      marker.position = val+dOffset;
    markers[0] = marker;
    double val2 = ui->lineEditMax->text().toDouble(&ok);
    if (markers.size() == 2 && ok)
    {
      this->ChangeLineEditNumber(ui->lineEditMid, (val+val2)/2);
    }
  }
  else if (sender() == ui->lineEditOffset)
  {
    for (int i = 0; i < markers.size(); i++)
    {
      LineMarker marker = markers[i];
      marker.position += (val - m_dSavedOffset);
      markers[i] = marker;
    }
    m_dSavedOffset = val;
  }
  ui->widgetHistogram->SetMarkers(markers);
  UpdateGraph();
}

void WindowConfigureOverlay::OnFrameChanged(int nFrame)
{
  if (sender() != ui->spinBoxFrame)
  {
    ui->spinBoxFrame->blockSignals(true);
    ui->spinBoxFrame->setValue(nFrame);
    ui->spinBoxFrame->blockSignals(false);
  }
  if (sender() != ui->sliderFrame)
  {
    ui->sliderFrame->blockSignals(true);
    ui->sliderFrame->setValue(nFrame);
    ui->sliderFrame->blockSignals(false);
  }
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    overlay->SetActiveFrame(nFrame);
    delete[] m_fDataCache;
    m_fDataCache = NULL;
    UpdateGraph();
    emit ActiveFrameChanged();
  }
}

void WindowConfigureOverlay::OnCurrentVertexChanged()
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay()
       && m_layerSurface->GetActiveOverlay()->GetNumberOfFrames() > 1 )
  {
    int nVertex = m_layerSurface->GetVertexIndexAtTarget( m_layerSurface->GetSlicePosition(), NULL );
    if (nVertex >= 0 && ui->checkBoxAutoFrame->isChecked()
        && nVertex < m_layerSurface->GetActiveOverlay()->GetNumberOfFrames())
    {
      OnFrameChanged(nVertex);

      // even if not auto apply, still call apply
      if (!ui->checkBoxAutoApply->isChecked())
        OnApply();
    }
  }
}

void WindowConfigureOverlay::OnCheckComputeCorrelation(bool bChecked)
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    if (bChecked)
    {
      int n = ui->comboBoxVolumes->currentIndex();
      if (n >= 0)
      {
        OnComboCorrelationVolume(n);
      }
    }
    overlay->SetComputeCorrelation(bChecked);
    delete[] m_fDataCache;
    m_fDataCache = NULL;

    UpdateGraph();
  }
}

void WindowConfigureOverlay::OnComboCorrelationVolume(int n)
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    LayerMRI* mri = qobject_cast<LayerMRI*>(ui->comboBoxVolumes->itemData(n).value<QObject*>());
    overlay->SetCorrelationSourceVolume(mri);
  }
}

void WindowConfigureOverlay::OnCheckUsePercentile(bool bChecked)
{
  if ( m_layerSurface && m_layerSurface->GetActiveOverlay() )
  {
    SurfaceOverlay* overlay = m_layerSurface->GetActiveOverlay();
    overlay->GetProperty()->SetUsePercentile(bChecked);
    ui->widgetHistogram->SetUsePercentile(bChecked);
  }
}

void WindowConfigureOverlay::OnCustomColorScale()
{
  ui->lineEditOffset->setText("0");
}
