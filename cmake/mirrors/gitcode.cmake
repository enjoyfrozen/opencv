# Tengine (Download via commit id)
ocv_update(TENGINE_GITCODE_PKG_MD5 1b5908632b557275cd6e85b0c03f9690)
# TBB (Download from release page)
ocv_update(TBB_GITCODE_RELEASE "v2020.2")
ocv_update(TBB_GITCODE_PKG_NAME "tbb-${TBB_GITCODE_RELEASE}")
ocv_update(TBB_GITCODE_PKG_MD5 4eeafdf16a90cb66e39a31c8d6c6804e)
# ADE (Download from release page)
ocv_update(ADE_GITCODE_RELEASE "v0.1.1f")
ocv_update(ADE_GITCODE_PKG_NAME "ade-${ADE_GITCODE_RELEASE}")
ocv_update(ADE_GITCODE_PKG_MD5 c12909e0ccfa93138c820ba91ff37b3c)

#
# extract commit id and package name from original usercontent download link,
# and replace the original download link with the one from gitcode
#
macro(ocv_download_url_gitcode_usercontent)
  string(REPLACE "/" ";" DL_URL_split ${DL_URL})
  list(GET DL_URL_split 5 __COMMIT_ID)
  list(GET DL_URL_split 6 __PKG_NAME)
  set(DL_URL "https://gitcode.net/opencv/opencv_3rdparty/-/raw/${__COMMIT_ID}/${__PKG_NAME}/")
endmacro()
#
# extract repo owner and repo name from original usercontent download link,
# and replace the original download link & md5 with the one from gitcode
#
macro(ocv_download_url_gitcode_archive_commit_id)
  string(REPLACE "/" ";" DL_URL_split ${DL_URL})
  list(GET DL_URL_split 3 __OWNER)
  list(GET DL_URL_split 4 __REPO_NAME)
  set(DL_URL "https://gitcode.net/${__OWNER}/${__REPO_NAME}/-/archive/")
  set(DL_HASH "${${DL_ID}_GITCODE_PKG_MD5}")
endmacro()
macro(ocv_download_url_gitcode_archive_release)
  string(REPLACE "/" ";" DL_URL_split ${DL_URL})
  list(GET DL_URL_split 3 __OWNER)
  list(GET DL_URL_split 4 __REPO_NAME)
  set(DL_URL "https://gitcode.net/${__OWNER}/${__REPO_NAME}/-/archive/${${DL_ID}_GITCODE_RELEASE}/${__REPO_NAME}-")
  set(DL_HASH "${${DL_ID}_GITCODE_PKG_MD5}")
endmacro()

if((DL_ID STREQUAL "FFMPEG") OR (DL_ID STREQUAL "IPPICV"))
  ocv_download_url_gitcode_usercontent()
elseif(DL_ID STREQUAL "TENGINE")
  ocv_download_url_gitcode_archive_commit_id()
elseif(DL_ID STREQUAL "TBB")
  ocv_download_url_gitcode_archive_release()
  set(OPENCV_TBB_SUBDIR "${TBB_GITCODE_PKG_NAME}" PARENT_SCOPE)
elseif(DL_ID STREQUAL "ADE")
  ocv_download_url_gitcode_archive_release()
  set(ade_subdir "${ADE_GITCODE_PKG_NAME}" PARENT_SCOPE)
else()
  message(STATUS "ocv_download: Unknown download ID ${DL_ID} for using mirror Gitcode.net. Use original source.")
endif()