#ifndef RME_MAP_DETAILS_DIALOG_H_
#define RME_MAP_DETAILS_DIALOG_H_

#include <wx/wx.h>

#include "io/iomap_otbm.h"

class ClientVersion;

class MapDetailsDialog : public wxDialog {
public:
	MapDetailsDialog(wxWindow* parent, const wxString& map_path, const OTBMStartupPeekResult* map_info, ClientVersion* client);
	~MapDetailsDialog() override = default;
};

#endif
