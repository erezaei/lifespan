# Contact: Matt McCormick <matt.mccormick@kitware.com>
itk_fetch_module(BridgeNumPy
  "This module contains wrapper code to convert ITK Image's to
  NumPy array's and back.  For example:

    import itk
    ImageType = itk.Image[itk.F, 2]
    image = ImageType.New()
    array = itk.PyBuffer[ImageType].GetArrayFromImage(image)
    image = itk.PyBuffer[ImageType].GetImageFromArray(array)

  See http://insight-journal.org/browse/publication/85
      https://hdl.handle.net/1926/188"
  GIT_REPOSITORY ${git_protocol}://github.com/InsightSoftwareConsortium/ITKBridgeNumPy.git
  GIT_TAG 6218266e76d00439bbad32d1d3acec069442aeab
  )
