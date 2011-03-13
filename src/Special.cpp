/*
	TinyCAD program for schematic capture
	Copyright 1994/1995/2002,2003 Matt Pyne.

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "stdafx.h"
#include "TinyCadView.h"
#include "special.h"
#include "library.h"
#include "TinyCad.h"
#include "DlgBOMExport.h"
#include "BOMGenerator.h"

////// The auto anotate special function //////

AnotateSetup theASetup;

// Auto anotate the design
void CTinyCadView::OnSpecialAnotate()
{
  CDlgAnotateBox theDialog(this,theASetup);

  // Get rid of any drawing tool
  GetCurrentDocument()->SelectObject(new CDrawEditItem(GetCurrentDocument()));

  // Do the dialog
  int action = theDialog.DoModal();
  
  if (action == IDC_REF_PAINTER)
  {
	  theASetup = theDialog.v;
	  GetCurrentDocument()->SelectObject(new CDrawRefPainter(GetCurrentDocument(), theASetup.startval ));
	  return;
  }
  else if (action !=IDOK)
  {
	return;
  }

  theASetup = theDialog.v;

  // Set the busy icon
  SetCursor( AfxGetApp()->LoadStandardCursor( IDC_WAIT ) );
  

  // Now add/remove references
  CDrawMethod *thisMethod;
  CSymbolRecord *thisSymbol;
  int value=0;
  int part=0;
  BOOL IsSet,IsMatch,MissingSymbols=FALSE;
  

	for (int i = 0; i < 2; i++)
	{
		int sheet = theASetup.all_sheets ? 0 : GetDocument()->GetActiveSheetIndex();

		do
		{
			drawingIterator it = GetDocument()->GetSheet(sheet)->GetDrawingBegin();
			while (it != GetDocument()->GetSheet(sheet)->GetDrawingEnd()) 
			{
				CDrawingObject *pointer = *it;
				// Look for method objects
				if (pointer->GetType() == xMethodEx3) 
				{
					thisMethod = (CDrawMethod *)pointer;
					thisSymbol = thisMethod->GetSymbolData();

					// If there is no symbol then cannot modify this symbol!
					if (thisMethod->IsNoSymbol()) 
					{
						MissingSymbols = TRUE;
						++ it;
						continue;
					}

					// Has this symbol got a reference?
					IsSet   = thisMethod->HasRef();

					switch (theASetup.reference)
					{
					case 0:		// All references
						IsMatch = TRUE;
						break;
					case 1:		// Un-numbered references
						IsMatch = !thisMethod->HasRef();
						break;
					case 2:		// References that match...
						IsMatch = theASetup.matchval == thisSymbol->reference;
						break;
					}

					if (IsMatch)
					{

						// First pass  - we remove references if necessary,
						// Second pass - we add refences back in...
						//
						if (i == 0)
						{
							// Remove any matching references (if necessary)
							if (IsSet && (theASetup.value!=1 || thisMethod->GetRefVal()>=theASetup.startval) ) 
							{
								thisMethod->RemoveReference();
							}
						}
						else
						{
							// Now add back any references
							if (theASetup.action == 0) 
							{
								if (theASetup.reference != 1)
								{
									value = (theASetup.value == 0) ? 1 : theASetup.startval;
								}
								thisMethod->AddReference( value, theASetup.all_sheets );
							}
						}
					}
				}

				++ it;
			}
			++ sheet;
		} while ( theASetup.all_sheets && sheet < GetDocument()->GetNumberOfSheets() );
	}

  // Where there any missing symbols?
  if (MissingSymbols)
	Message(IDS_MISSMETH,MB_ICONEXCLAMATION);


  // Restore the correct cursor
  SetCursor( AfxGetApp()->LoadStandardCursor( IDC_ARROW ) );


  // Ensure the window is re-drawn
  GetCurrentDocument()->SetModifiedFlag( TRUE );
  Invalidate();  
}



// The Anotate Setup values class
AnotateSetup::AnotateSetup()
{
  action=0;
  reference=0;
  value=0;
  matchval="U?";
  startval=1;
  all_sheets=false;
}





////// The Parts List (Bill of Materials) special function //////


void CTinyCadView::OnSpecialBom()
{
  // Get rid of any drawing tool
  GetCurrentDocument()->SelectObject(new CDrawEditItem(GetCurrentDocument()));


	// Get the file in which to save the network
	TCHAR szFile[256];
	_tcscpy_s(szFile,GetDocument()->GetPathName());
	TCHAR* ext = _tcsrchr(szFile,'.');
	if (!ext)
	{
		_tcscpy_s(szFile,_T("output.txt"));
	}
	else
	{
		size_t remaining_space = &szFile[255] - ext + 1;
		_tcscpy_s(ext, remaining_space, _T(".txt"));
	}

	// Get the file name for the parts list
	CDlgBOMExport dlg(this);
	dlg.m_Filename = szFile;

	if (dlg.DoModal() != IDOK)
	{
	  return;
	}

  FILE *theFile;
  errno_t err;

  err = _tfopen_s(&theFile, dlg.m_Filename,_T("w"));
  if ((theFile == NULL) || (err != 0)) 
  {
	Message(IDS_CANNOTOPEN);
	return;
  }

  // Set the Busy icon
  SetCursor( AfxGetApp()->LoadStandardCursor( IDC_WAIT ) );


  CBOMGenerator	bom;
  bom.GenerateBomForDesign( dlg.m_All_Sheets != 0, 
	  dlg.m_All_Attrs != 0, dlg.m_Prefix != 0, dlg.m_Hierarchical != 0, GetDocument() );

  // Now generate the output file
  bom.WriteToFile( theFile, dlg.m_type == 1 );

  fclose(theFile);

  // Restore the normal cursor
  SetCursor( AfxGetApp()->LoadStandardCursor( IDC_ARROW ) );

  // Where there any errors?
  if (bom.GetMissingRef())
	Message(IDS_MISSREF);

  CTinyCadApp::EditTextFile( dlg.m_Filename );
}




