#include "io.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QObject>
#include <QtCore/QRegExp>

#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <OSD_Path.hxx>
#include <RWStl.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx> // For IGES files reading
#include <IGESControl_Writer.hxx>
#include <STEPControl_Reader.hxx> // For STEP files reading
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx> // For status reading
#include <Interface_Static.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Transfer_FinderProcess.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XSControl_WorkSession.hxx>

namespace {

template<typename _READER_>
TopoDS_Shape loadFile(const QString& fileName, Handle_Message_ProgressIndicator indicator)
{
  TopoDS_Shape result;

  if (!indicator.IsNull())
    indicator->NewScope(30, "Loading file");
  _READER_ reader;
  const int status = reader.ReadFile(const_cast<Standard_CString>(qPrintable(fileName)));
  if (!indicator.IsNull())
    indicator->EndScope();
  if (status == IFSelect_RetDone) {
    if (!indicator.IsNull()) {
      reader.WS()->MapReader()->SetProgress(indicator);
      indicator->NewScope(70, "Translating file");
    }
    reader.NbRootsForTransfer();
    reader.TransferRoots();
    result = reader.OneShape();
    if (!indicator.IsNull()) {
      indicator->EndScope();
      reader.WS()->MapReader()->SetProgress(0);
    }
  }
  return result;
}

} // Anonymous namespace

namespace occ {

IO::Format IO::partFormat(const QString& filename)
{
  const QString suffix(QFileInfo(filename).suffix().toLower());
  if (suffix == "step" || suffix == "stp")
    return StepFormat;
  else if (suffix == "iges" || suffix == "igs")
    return IgesFormat;
  else if (suffix == "brep" || suffix == "rle")
    return OccBRepFormat;
  else if (suffix == "stla")
    return AsciiStlFormat;
  else if (suffix == "stlb")
    return BinaryStlFormat;

  // Failed to deduce the format from the suffix, try by investigating the
  // contents
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly))
    return UnknownFormat;
  const QByteArray contentsBegin = file.read(2048);
  // -- Assume a text-based format
  const QString contentsBeginText(contentsBegin);
  if (contentsBeginText.contains(QRegExp("^.{72}S\\s*[0-9]+\\s*[\\n\\r\\f]")))
    return IgesFormat;
  if (contentsBeginText.contains(QRegExp("^\\s*ISO-10303-21\\s*;\\s*HEADER")))
    return StepFormat;
  if (contentsBeginText.contains(QRegExp("^\\s*DBRep_DrawableShape")))
    return OccBRepFormat;
  if (contentsBeginText.contains(QRegExp("^\\s*solid")))
    return AsciiStlFormat;
  // -- Assume a binary-based format
  // -- -- Binary STL ?
  const int binaryStlHeaderSize = 80 + sizeof(quint32);
  if (contentsBegin.size() >= binaryStlHeaderSize) {
    QBuffer buffer;
    buffer.setData(contentsBegin);
    buffer.open(QIODevice::ReadOnly);
    buffer.seek(80); // Skip header
    quint32 facetsCount = 0;
    buffer.read(reinterpret_cast<char*>(&facetsCount), sizeof(quint32));
    const unsigned facetSize = (sizeof(float) * 12) + sizeof(quint16);
    if ((facetSize * facetsCount + binaryStlHeaderSize) == file.size())
      return BinaryStlFormat;
  }
  // Fallback case
  return UnknownFormat;
}

const TopoDS_Shape IO::loadPartFile(const QString& filename)
{
  switch (partFormat(filename)) {
  case StepFormat:
    return IO::loadStepFile(filename);
  case IgesFormat:
    return IO::loadIgesFile(filename);
  case OccBRepFormat:
    return IO::loadBrepFile(filename);
  default:
    return TopoDS_Shape();
  }
  return TopoDS_Shape();
}

const Handle_StlMesh_Mesh IO::loadStlFile(const QString& filename)
{
  return RWStl::ReadFile(OSD_Path(filename.toAscii().data()));
}

/*! \brief Topologic shape read from a file (OCC's internal BREP format)
 *  \param fileName Path to the file to read
 *  \param indicator Indicator to notify the loading progress
 *  \return The part as a whole topologic shape
 */
TopoDS_Shape IO::loadBrepFile(const QString& fileName,
                              Handle_Message_ProgressIndicator indicator)
{
  TopoDS_Shape result;
  BRep_Builder brepBuilder;
  BRepTools::Read(result, fileName.toAscii().data(), brepBuilder, indicator);
  return result;
}

/*! \brief Topologic shape read from a file (IGES format)
 *  \param fileName Path to the file to read
 *  \param indicator Indicator to notify the loading progress
 *  \return The part as a whole topologic shape
 */
TopoDS_Shape IO::loadIgesFile(const QString& fileName,
                              Handle_Message_ProgressIndicator indicator)
{
  return ::loadFile<IGESControl_Reader>(fileName, indicator);
}

/*! \brief Topologic shape read from a file (STEP format)
 *  \param fileName Path to the file to read
 *  \param indicator Indicator to notify the loading progress
 *  \return The part as a whole topologic shape
 */
TopoDS_Shape IO::loadStepFile(const QString& fileName,
                              Handle_Message_ProgressIndicator indicator)
{
  return ::loadFile<STEPControl_Reader>(fileName, indicator);
}

/*! \brief Write a topologic shape to a file (OCC's internal BREP format)
 *  \param shape Topologic shape to write
 *  \param fileName Path to the file to write
 *  \param indicator Indicator to notify the writing progress
 */
void IO::writeBrepFile(const TopoDS_Shape& shape,
                       const QString& fileName,
                       Handle_Message_ProgressIndicator indicator)
{
  BRepTools::Write(shape, fileName.toAscii().data(), indicator);
}

/*! \brief Write a topologic shape to a file (IGES format)
 *  \param shape Topologic shape to write
 *  \param fileName Path to the file to write
 *  \param indicator Indicator to notify the writing progress
 */
void IO::writeIgesFile(const TopoDS_Shape& shape,
                       const QString& fileName,
                       Handle_Message_ProgressIndicator indicator)
{
  IGESControl_Controller::Init();
  IGESControl_Writer writer(Interface_Static::CVal("XSTEP.iges.unit"),
                            Interface_Static::IVal("XSTEP.iges.writebrep.mode"));
  if (!indicator.IsNull())
    writer.TransferProcess()->SetProgress(indicator);
  writer.AddShape(shape);
  writer.ComputeModel();
  writer.Write(fileName.toAscii().data());
  writer.TransferProcess()->SetProgress(0);
}

/*! \brief Write a topologic shape to a file (STEP format)
 *  \param shape Topologic shape to write
 *  \param fileName Path to the file to write
 *  \param indicator Indicator to notify the writing progress
 */
void IO::writeStepFile(const TopoDS_Shape& shape,
                       const QString& fileName,
                       Handle_Message_ProgressIndicator indicator)
{
  IFSelect_ReturnStatus status;
  STEPControl_Writer writer;
  if (!indicator.IsNull())
    writer.WS()->MapReader()->SetProgress(indicator);
  status = writer.Transfer(shape, STEPControl_AsIs);
  status = writer.Write(fileName.toAscii().data());
  writer.WS()->MapReader()->SetProgress(0);
}

/*! \brief Write a topologic shape to a file (ASCII STL format)
 *  \param shape Topologic shape to write
 *  \param fileName Path to the file to write
 */
void IO::writeAsciiStlFile(const TopoDS_Shape& shape, const QString& fileName)
{
  StlAPI_Writer writer;
  writer.ASCIIMode() = Standard_True;
  writer.Write(shape, fileName.toAscii().data());
}

/*! \brief Write a topologic shape to a file (binary STL format)
 *  \param shape Topologic shape to write
 *  \param fileName Path to the file to write
 */
void IO::writeBinaryStlFile(const TopoDS_Shape& shape, const QString& fileName)
{
  StlAPI_Writer writer;
  writer.ASCIIMode() = Standard_False;
  writer.Write(shape, fileName.toAscii().data());
}

} // namespace occ
