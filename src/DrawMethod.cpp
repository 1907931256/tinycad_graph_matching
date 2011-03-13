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



// This handles the actual drawing of composite objects (symbols)

#include "stdafx.h"
#include "TinyCad.h"
#include "TinyCadView.h"
#include "diag.h"
#include "colour.h"
#include "option.h"
#include <math.h>
#include "JunctionUtils.h"
#include "LibraryCollection.h"
#include "ListOfFonts.h"
#include "DlgReplaceBox.h"

// The Method Class //

// NB a = offset of the bottom left hand corner (used to define position)
//    b = offset of the top right hand corner (used to define size)

// The constructor
CDrawMethod::CDrawMethod(CTinyCadDoc *pDesign,hSYMBOL symbol,int new_rotation)
: CDrawingObject( pDesign )
{
  part=0;
  m_segment=1;
  scaling_x = 1.0;
  scaling_y = 1.0;
  can_scale = FALSE;
  show_power = FALSE;
  rotate=new_rotation;
	
  m_Symbol = symbol;

  CSymbolRecord *newSymbol = GetSymbolData();
  
  m_point_a = CDPoint(0,0);
  m_point_b = GetTr();

  m_fields.resize( 2 );

  m_fields[Name].m_value=newSymbol->name;
  m_fields[Name].m_description="Name";
  m_fields[Name].m_show= newSymbol->name_type == default_show;
  m_fields[Name].m_position = CDPoint(0,0);
  m_fields[Name].m_type = newSymbol->name_type;

  m_fields[Ref].m_value=newSymbol->reference;
  m_fields[Ref].m_description="Ref";
  m_fields[Ref].m_show=newSymbol->ref_type == default_show;
  m_fields[Ref].m_position = CDPoint(0,0);
  m_fields[Ref].m_type = newSymbol->ref_type;

  // Now load in the other fields
  for (unsigned int i = 0; i < newSymbol->fields.size(); i++)
  {
	  CField f;
	  f.m_description = newSymbol->fields[i].field_name;
	  f.m_value = newSymbol->fields[i].field_default;
	  f.m_type = newSymbol->fields[i].field_type;
	  f.m_position = CDPoint(0,0);
	  f.m_show = f.m_type == default_show;

	  m_fields.push_back( f );
  }

  NewRotation();

}


// Tag the resources being used by this symbol
void CDrawMethod::TagResources()
{
 	m_pDesign->GetOptions()->TagFont(fPIN);
	m_pDesign->GetOptions()->TagSymbol( m_Symbol );

	CDPoint tr;
	drawingCollection method;
	ExtractSymbol( tr, method );

	drawingIterator it = method.begin();
	while (it != method.end()) 
	{
		(*it)->TagResources();
		++ it;
	}

}


int CDrawMethod::GetContextMenu()
{
	return IDR_METHOD_EDIT;
}


void CDrawMethod::ContextMenu( CDPoint p, UINT id )
{
	switch (id)
	{
	case ID_CONTEXT_REPLACESYMBOL:
		{
			CSymbolRecord *pSymbol = GetSymbolData();
			CDlgReplaceBox	dlg( AfxGetMainWnd() );
			dlg.m_search_string = pSymbol->name;
			if (dlg.DoModal() == IDOK && dlg.GetSymbol() != NULL)
			{
				m_pDesign->BeginNewChangeSet();
				hSYMBOL new_symbol = m_pDesign->GetOptions()->AddSymbol( dlg.GetSymbol()->GetDesignSymbol(m_pDesign) );

				// Replace this symbol with the new one...
				if (dlg.m_all_symbols != 0)
				{
					m_pDesign->ReplaceSymbol( m_Symbol, new_symbol, dlg.m_keep_old_fields != 0 );
				}
				else
				{
					ReplaceSymbol( m_Symbol, new_symbol, dlg.m_keep_old_fields != 0 );
				}

				// Update the method edit box
				g_EditToolBar.m_MethodEdit.ReadFields();
			}
		}
		break;
	}
}


// Replace the current symbol with a new one
void CDrawMethod::ReplaceSymbol( hSYMBOL old_symbol, hSYMBOL new_symbol, bool keep_old_fields )
{
	// Do we need to action this?
	if (old_symbol != m_Symbol)
	{
		// no...
		return;
	}

	// Signal change for undo
	m_pDesign->MarkChangeForUndo( this );

	// First replace the symbol
	m_Symbol = new_symbol;

	// Now copy over any relevant fields...
	CSymbolRecord *pSymbol = GetSymbolData();
  

	// Remove any hidden fields first
	for (unsigned int j = 2; j < m_fields.size(); j++)
	{
		// Is this a hidden symbol?
		if (m_fields[j].m_type == always_hidden)
		{
			// Remove this field from our symbol
			m_fields.erase( m_fields.begin() + j );
			j --;
		}
		else if (m_fields[j].m_type != extra_parameter)
		{
			if (keep_old_fields)
			{
				// Remove any fields that are not in the new symbol...
				bool found = false;
				for (unsigned int i = 0; i < pSymbol->fields.size(); i++)
				{
					if (pSymbol->fields[i].field_name == m_fields[j].m_description)
					{
						found = true;
						break;
					}
				}

				// If it isn't in the new symbol delete it...
				if (!found)
				{
					m_fields.erase( m_fields.begin() + j );
					-- j;
				}
			}
			else
			{
				// Erase all of the old fields that the user didn't add
				m_fields.erase( m_fields.begin() + j );
				-- j;
			}
		}
	}


	// Add in any fields that are not already part of this symbol
	for (unsigned int i = 0; i < pSymbol->fields.size(); i++)
	{
		CField f;
		f.m_description = pSymbol->fields[i].field_name;
		f.m_value = pSymbol->fields[i].field_default;
		f.m_type = pSymbol->fields[i].field_type;
		f.m_position = CDPoint(0,0);
		f.m_show = f.m_type == default_show;

		// Does this new field already exist?
		bool found = false;
		for (unsigned int j = 0; j < m_fields.size(); j++)
		{
			if (m_fields[j].m_description == f.m_description)
			{
				// Enforce the same type...
				m_fields[j].m_type = f.m_type;
				found = true;
				break;
			}
		}

		if (!found)
		{
			m_fields.push_back( f );
		}
	}
}


// Look for a seach string in the object
CString CDrawMethod::Find(const TCHAR *theSearchString)
{
  CString HoldString;

  for (unsigned int lp =0;lp<m_fields.size();lp++) 
  {
  	HoldString = m_fields[lp].m_value;
  	HoldString.MakeLower();

  	// Now look for the search string in this string
	if (HoldString.Find(theSearchString) != -1)
  		return m_fields[lp].m_value;
  }

  return "";
}



// Rotate this object about a point
void CDrawMethod::Rotate(CDPoint p,int ndir)
{
  // Translate this point so the rotational point is the origin
  m_point_a = CDPoint(m_point_a.x-p.x,m_point_a.y-p.y);
  m_point_b = CDPoint(m_point_b.x-p.x,m_point_b.y-p.y);

  // Perfrom the rotation
  switch (ndir) {
	case 2: // Left
		m_point_a = CDPoint(m_point_a.y,-m_point_a.x);
		m_point_b = CDPoint(m_point_b.y,-m_point_b.x);
		break;		
	case 3: // Right
		m_point_a = CDPoint(-m_point_a.y,m_point_a.x);
		m_point_b = CDPoint(-m_point_b.y,m_point_b.x);
		break;
	case 4: // Mirror
		m_point_a = CDPoint(-m_point_a.x,m_point_a.y);
		m_point_b = CDPoint(-m_point_b.x,m_point_b.y);
		rotate ^= 4;
		break;
  }

  // Re-translate the points back to the original location
  m_point_a = CDPoint(m_point_a.x+p.x,m_point_a.y+p.y);
  m_point_b = CDPoint(m_point_b.x+p.x,m_point_b.y+p.y);

  // Set the a and b co-ords to their correct arrangements
  CDPoint la = m_point_a;

  m_point_a = CDPoint(max(la.x,m_point_b.x),max(la.y,m_point_b.y));
  m_point_b = CDPoint(min(la.x,m_point_b.x),min(la.y,m_point_b.y));


  rotate = DoRotate(rotate&3,ndir) | (rotate&4);

  NewRotation();
}


int CDrawMethod::DoRotate(int olddir,int newdir)
{
  //     New rotation=> 2  3  4  
  //				    Current dir ..\/..
  int table[] = {	2, 3, 0, 		// 0 (Up)
			3, 2, 1,		// 1 (Down)
			1, 0, 3,		// 2 (Left)
			0, 1, 2,		// 3 (Right)
		};

  return table[(newdir-2) + olddir*3];
}


// Another Constructor
CDrawMethod::CDrawMethod(CTinyCadDoc *pDesign)
: CDrawingObject( pDesign )
{
	m_Symbol = 0;
  rotate=0;

  m_point_a=m_point_b=CDPoint(0,0);

  m_fields.resize( 3 );

  m_fields[Name].m_value="";
  m_fields[Name].m_description="Name";
  m_fields[Name].m_show=TRUE;
  m_fields[Name].m_position = CDPoint(0,0);
  m_fields[Name].m_type = default_show;

  m_fields[Other].m_value="";
  m_fields[Other].m_description="Other";
  m_fields[Other].m_show=FALSE;
  m_fields[Other].m_position = CDPoint(0,0);
  m_fields[Other].m_type = default_hidden;

  m_fields[Ref].m_value="";
  m_fields[Ref].m_description="Ref";
  m_fields[Ref].m_show=TRUE;
  m_fields[Ref].m_position = CDPoint(0,0);
  m_fields[Ref].m_type = default_show;

  rotate=3;
  part=0;

  m_segment=0;

  scaling_x = 1.0;
  scaling_y = 1.0;
  can_scale = FALSE;
  show_power = FALSE;
}


BOOL CDrawMethod::ExtractSymbol( CDPoint &tr, drawingCollection &method )
{
	if (!GetSymbolData()->GetMethod( part, show_power != 0, method ))
	{
		return FALSE;
	}

	tr = GetSymbolData()->GetTr( part, show_power != 0 );
	tr = CDPoint(tr.x * scaling_x, tr.y * scaling_y );

	// Calculate the bounding box
	if ((rotate&3)<2)
	{
		m_point_b=CDPoint(tr.x+m_point_a.x,tr.y+m_point_a.y);
	}
	else
	{
		m_point_b=CDPoint(tr.y+m_point_a.x,tr.x+m_point_a.y);
	}

	return TRUE;
}

BOOL CDrawMethod::IsNoSymbol()
{
	return m_Symbol == 0;
}


// Get the method to draw this symbol
CDesignFileSymbol *CDrawMethod::GetSymbolData()
{
	return m_pDesign->GetOptions()->GetSymbol( m_Symbol );
}


// Get the reference value
int CDrawMethod::GetRefVal()
{
  return _tstoi(GetRef().Mid(GetRef().FindOneOf(_T("0123456789"))));
}

// Set the reference value
void CDrawMethod::SetRefVal(int value)
{
  TCHAR Buffer[10];

  _itot_s(value,Buffer,10);
  m_fields[Ref].m_value = GetSymbolData()->reference;
  m_fields[Ref].m_value 
	  = m_fields[Ref].m_value.Left(m_fields[Ref].m_value.ReverseFind('?')) + Buffer;
}



BOOL CDrawMethod::CanEdit()
{
  return TRUE;
}

CString CDrawMethod::GetName() const
{
  return m_fields[Name].m_value;
}


void CDrawMethod::BeginEdit(BOOL re_edit)
{
  g_EditToolBar.m_MethodEdit.Open(m_pDesign,this);
}

void CDrawMethod::EndEdit()
{
  RButtonDown(CDPoint(0,0), CDPoint(0,0));
  g_EditToolBar.m_MethodEdit.Close();
}


CString	CDrawMethod::GetFieldName(int which)
{
	return m_fields[which].m_description;
}


CString CDrawMethod::GetField(int which)
{
  CString r = m_fields[which].m_value;

  if (which == Ref && GetSymbolData()->ppp!=1) {
	TCHAR Apart[2];
	if (GetSymbolData()->ppp>1) {
		Apart[0]=part + 'A';
		Apart[1]=0;
	} else
		Apart[0]=0;
	r += Apart;
  }

  return r;
}
//-------------------------------------------------------------------------
void CDrawMethod::GetSymbolByName( const TCHAR *SymName )
{
	CLibraryStoreNameSet* pSymbol = CLibraryCollection::GetSymbol( SymName );

  	if( pSymbol == NULL )
  {
		// Use the no symbol...
	  	m_Symbol = 0;
  }
  else
  {
		m_Symbol = m_pDesign->GetOptions()->AddSymbol( pSymbol->GetDesignSymbol(m_pDesign, 0) );
  }
}
//-------------------------------------------------------------------------
void CDrawMethod::OldLoad(CStream &archive)
{
	CString SymName;
  BYTE Shows;

  m_fields.resize(3);

  archive >> SymName 
	  >> m_fields[Name].m_value 
	  >> m_fields[Other].m_value 
	  >> m_fields[Ref].m_value;
  m_fields[Name].m_position  = ReadPoint(archive);
  m_fields[Other].m_position = ReadPoint(archive);
  m_fields[Ref].m_position   = ReadPoint(archive);

  m_fields[Other].m_description="Other";
  m_fields[Other].m_type = default_hidden;

  archive >> Shows >> rotate;
  m_point_a = ReadPoint(archive);
  archive >> part;

  m_fields[Name].m_show  = (Shows & 1)!=0;
  m_fields[Other].m_show = (Shows & 2)!=0;
  m_fields[Ref].m_show   = (Shows & 4)!=0;

  GetSymbolByName( SymName );  

  m_segment = 0;

  // Now get the calculation of the bounding box
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );
}


void CDrawMethod::OldLoad2(CStream &archive)
{
  BYTE Shows;
  CString attrib;
  CString SymName;

  m_fields.resize( 3 );

  archive >> SymName 
	  >> m_fields[Name].m_value 
	  >> m_fields[Other].m_value 
	  >> m_fields[Ref].m_value
	  >> attrib;
  m_fields[Name].m_position  = ReadPoint(archive);
  m_fields[Other].m_position = ReadPoint(archive);
  m_fields[Ref].m_position   = ReadPoint(archive);

  m_fields[Other].m_description="Other";
  m_fields[Other].m_type = default_hidden;

  archive >> Shows >> rotate;
  m_point_a = ReadPoint(archive);
  archive >> part;

  m_fields[Name].m_show  = (Shows & 1)!=0;
  m_fields[Other].m_show = (Shows & 2)!=0;
  m_fields[Ref].m_show   = (Shows & 4)!=0;

  GetSymbolByName( SymName );

  m_segment = 0;

  // Now get the calculation of the bounding box
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );
}

void CDrawMethod::OldLoad3(CStream &archive)
{
  BYTE Shows;

  CString attrib;

  m_fields.resize(3);

  archive >> m_Symbol;
  archive >> m_fields[Name].m_value 
	  >> m_fields[Other].m_value 
	  >> m_fields[Ref].m_value
	  >> attrib;
  m_fields[Name].m_position  = ReadPoint(archive);
  m_fields[Other].m_position = ReadPoint(archive);
  m_fields[Ref].m_position   = ReadPoint(archive);

  m_fields[Other].m_description="Other";
  m_fields[Other].m_type = default_hidden;

  archive >> Shows >> rotate;
  m_point_a = ReadPoint(archive);
  archive >> part;

  // Read in the special other text
  std::vector<CSymbolField> fields;
  WORD special_text_count;
  archive >> special_text_count;
  int i;
  for (i = 0; i < special_text_count; i++)
  {
	  CSymbolField sf;
	  sf.Load( archive );
	  fields.push_back( sf );
  }

  for (i = 0; i < special_text_count; i++)
  {
	  if (fields[i].field_name == "SCALING_X")
	  {
		  scaling_x = _tstof(fields[i].field_value);
	  }
	  else if (fields[i].field_name == "SCALING_Y")
	  {
		  scaling_y = _tstof(fields[i].field_value);
	  }
  }


  m_fields[Name].m_show  = (Shows & 1)!=0;
  m_fields[Other].m_show = (Shows & 2)!=0;
  m_fields[Ref].m_show   = (Shows & 4)!=0;

  m_Symbol = m_pDesign->GetOptions()->GetNewSymbolNumber( m_Symbol );

  m_segment = 0;


  // Now get the calculation of the bounding box
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );
}


const TCHAR* CDrawMethod::GetXMLTag()
{
	return _T("SYMBOL");
}

// Load and save to an XML file
void CDrawMethod::SaveXML( CXMLWriter &xml )
{
	xml.addTag(GetXMLTag());

	xml.addAttribute( _T("id"), m_Symbol );
	xml.addAttribute( _T("pos"), CDPoint(m_point_a) );
	xml.addAttribute( _T("part"), part );
	xml.addAttribute( _T("rotate"), rotate );
	xml.addAttribute( _T("can_scale"), can_scale );
	xml.addAttribute( _T("show_power"), show_power );
	xml.addAttribute( _T("scale_x"), scaling_x );
	xml.addAttribute( _T("scale_y"), scaling_y );


	// Now read in the fields
	int field_size = m_fields.size();;
	for (unsigned int i = 0; i < m_fields.size(); i++)
	{
		CField &f = m_fields[i];

		int t = static_cast<int>(f.m_type);

		xml.addTag( _T("FIELD") );
		xml.addAttribute( _T("type") , t );
		xml.addAttribute( _T("description"),  f.m_description );
		xml.addAttribute( _T("value"), f.m_value );
		xml.addAttribute( _T("show"), f.m_show );
		xml.addAttribute( _T("pos"),  CDPoint( f.m_position ) );
		xml.closeTag();
	}

	xml.closeTag();
}

void CDrawMethod::LoadXML( CXMLReader &xml )
{
	xml.getAttribute( _T("id"), m_Symbol );
	xml.getAttribute( _T("pos"), m_point_a );
	xml.getAttribute( _T("part"), part );
	xml.getAttribute( _T("rotate"), rotate );
	xml.getAttribute( _T("can_scale"), can_scale );
	xml.getAttribute( _T("show_power"), show_power );
	xml.getAttribute( _T("scale_x"), scaling_x );
	xml.getAttribute( _T("scale_y"), scaling_y );


	xml.intoTag();
	int i = 0;
	CString name;
	while (xml.nextTag(name))
	{
		if (name == _T("FIELD"))
		{
			m_fields.resize( i + 1 );
			CField &f = m_fields[i];

			int t = 0;
			xml.getAttribute( _T("type"), t );
			xml.getAttribute( _T("description"),  f.m_description );
			xml.getAttribute( _T("value"), f.m_value );
			xml.getAttribute( _T("show"), f.m_show );
			xml.getAttribute( _T("pos"),  f.m_position );
			f.m_type = static_cast<SymbolFieldType>(t);

			++ i;
		}
	}
	xml.outofTag();


	// Use this data to locate our symbol
	m_Symbol = m_pDesign->GetOptions()->GetNewSymbolNumber( m_Symbol );

	m_segment = 0;

	// Now get the calculation of the bounding box
	CDPoint tr;
	drawingCollection method;
	ExtractSymbol( tr, method );

}



void CDrawMethod::Load(CStream &archive)
{
  int load_version;

  // Pull in the method revision number
  // (This supercedes the need for MethodEx, Ex2, Ex3 etc..)
  archive >> load_version;

  // Now write out our symbol number
  archive >> m_Symbol;

  // Our location
  m_point_a = ReadPoint(archive);

  // Part in package...
  archive >> part;

  // Our rotation
  archive >> rotate;

  // Scaling
  archive >> can_scale >> scaling_x >> scaling_y;

  // Now read in the fields
  int field_size;
  archive >> field_size;
  m_fields.resize( field_size );
  for (unsigned int i = 0; i < m_fields.size(); i++)
  {
	  CField &f = m_fields[i];
	  CPoint p;

	  int t;
	  archive >> t;
	  f.m_type = static_cast<SymbolFieldType>(t);

	  archive 
		>> f.m_description
		>> f.m_value
		>> f.m_show
		>> p;
	  f.m_position = CDPoint( p.x, p.y );
  }


  // Use this data to locate our symbol
  m_Symbol = m_pDesign->GetOptions()->GetNewSymbolNumber( m_Symbol );

  m_segment = 0;

  // Now get the calculation of the bounding box
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );
}



inline void swap(int &a,int &b) { int sp; sp=a; a=b; b=sp; }
inline void swap(double &a,double &b) { double sp; sp=a; a=b; b=sp; }

double CDrawMethod::DistanceFromPoint( CDPoint p )
{
	// If definately inside this item then return 0
	if (IsInside( p.x,p.x, p.y,p.y ))
	{
		return 0.0;
	}

	// Ok, try a looser fit (and return 20 if we do fit...)
   CDPoint tr;
   drawingCollection method;
   ExtractSymbol( tr, method );

  // Use fast cut-off to see if the bounding box is inside
  // the intersection box
  if ( !((m_point_a.x<p.x && m_point_b.x<=p.x) || (m_point_a.x>p.x && m_point_b.x>p.x)
    || (m_point_a.y<p.y && m_point_b.y<=p.y) || (m_point_a.y>p.y && m_point_b.y>p.y))) 
  {
	return 15.0;
  }


  return 100.0;
}



BOOL CDrawMethod::IsInside(double left,double right,double top,double bottom)
{
   CDPoint tr;
   drawingCollection method;
   ExtractSymbol( tr, method );


  // Use fast cut-off to see if the bounding box is inside
  // the intersection box
  if ( !((m_point_a.x<left && m_point_b.x<=left) || (m_point_a.x>right && m_point_b.x>right)
    || (m_point_a.y<top && m_point_b.y<=top) || (m_point_a.y>bottom && m_point_b.y>bottom))) {
	double nl,nr,nt,nb;

	// Translate the intersection box
	double sx = scaling_x;
	double sy = scaling_y;
	if ((rotate&3) >= 2)
	{
		sy = scaling_x;
		sx = scaling_y;
	}

	nl = (left-m_point_a.x) / sx;
	nr = (right-m_point_a.x) / sx;

	nt = (top-m_point_a.y) / sy;
	nb = (bottom-m_point_a.y) / sy;

	// Rotate the intersection box
	switch (rotate&3) {
		case 1:		// Down
			swap(nt,nb);
			nt=-(nt-tr.y/sy);
			nb=-(nb-tr.y/sy);
			break;
		case 2:		// Left
			swap(nt,nl);
			swap(nb,nr);
			break;
		case 3:		// Right
			swap(nt,nl);
			swap(nb,nr);
			swap(nt,nb);
			nt=-(nt-tr.y/sx);
			nb=-(nb-tr.y/sx);
			break;
	}

	// Mirror the intersection box
	if ((rotate&4)!=0) {
		if ((rotate&3) >= 2)
		{
			nl=-nl+tr.x/sy;
			nr=-nr+tr.x/sy;
			swap(nl,nr);
		}
		else
		{
			nt=-nt+tr.y/sy;
			nb=-nb+tr.y/sy;
			swap(nb,nt);
		}
	}

	// Search each element until one is inside
	drawingIterator it = method.begin();
	while (it != method.end())
	{
		CDrawingObject *p = *it;
		if (p->IsInside(nl,nr,nt,nb))
		{
			break;
		}

		++ it;
	}

	if (it != method.end())
		return TRUE;
  }

  unsigned int lp;
  for (lp=0;lp < m_fields.size();lp++) {
	if (!m_fields[lp].m_show)
	{
		continue;
	}

	CDPoint p1,p2;

	CDSize size=m_pDesign->GetTextExtent(GetField(lp),fPIN);

	p1 = CDPoint(m_fields[lp].m_position.x+m_point_a.x,m_fields[lp].m_position.y+m_point_a.y);
	p2 = CDPoint(m_fields[lp].m_position.x+m_point_a.x+size.cx,m_fields[lp].m_position.y+m_point_a.y-size.cy);
	if ( !( (p1.x<left && p2.x<=left) || (p1.x>right && p2.x>right)
	     || (p1.y<top && p2.y<=top) || (p1.y>bottom && p2.y>bottom)) )
		return TRUE;

  }

  return FALSE;
}


ObjType CDrawMethod::GetType()
{
  return xMethodEx3;
}


void CDrawMethod::Move(CDPoint p, CDPoint no_snap_p)
{
  // r is the relative displacement to move
  CDPoint r;

  r.x=p.x-m_point_a.x;
  r.y=p.y-m_point_a.y;

  if (r.x!=0 || r.y!=0) 
  {
	Display();

	m_point_a=p;
	m_point_b=CDPoint(m_point_b.x+r.x,m_point_b.y+r.y);

	m_segment=0;
	Display();
  }
}


void CDrawMethod::LButtonDown(CDPoint p, CDPoint no_snap_p)
{
  // New undo level for each placement...
  m_pDesign->BeginNewChangeSet();

  Display();
  Move(p, no_snap_p);
  Store();

  CJunctionUtils j(m_pDesign);
  j.AddObjectToTodo( this );
  j.CheckTodoList( true );
 
  Display();	// Write to screen
}



void CDrawMethod::MoveField(int w, CDPoint r)
{
  Display();

  if (w >= 1000)
  {
	  m_fields[w - 1000].m_position.x += r.x;
	  m_fields[w - 1000].m_position.y += r.y;
	  Display();
  }
  else
  {
	CDRect rect( m_point_a.x, m_point_a.y, m_point_b.x, m_point_b.y );

	switch (w)
	{
	case CRectTracker::hitTopLeft:
		rect.left += r.x;
		rect.top += r.y;
		break;
	case CRectTracker::hitTopRight:
		rect.right += r.x;
		rect.top += r.y;
		break;
	case CRectTracker::hitBottomRight:
		rect.right += r.x;
		rect.bottom += r.y;
		break;
	case CRectTracker::hitBottomLeft:
		rect.left += r.x;
		rect.bottom += r.y;
		break;	
	case CRectTracker::hitTop:
		rect.top += r.y;
		break;		
	case CRectTracker::hitRight:
		rect.right += r.x;
		break;		
	case CRectTracker::hitBottom:
		rect.bottom += r.y;
		break;		
	case CRectTracker::hitLeft:
		rect.left += r.x;
		break;		
	}

	// Determine scaling in the x and y
	CDPoint tr;
	scaling_x = 1.0;
	scaling_y = 1.0;
	drawingCollection method;
	ExtractSymbol( tr, method );
	scaling_x = fabs(static_cast<double>(rect.Width()) / fabs( static_cast<double>(m_point_a.x - m_point_b.x)) );
	scaling_y = fabs(static_cast<double>(rect.Height()) / fabs( static_cast<double>(m_point_a.y - m_point_b.y)) );

	if ((rotate&3) >= 2)
	{
		double q = scaling_x;
		scaling_x = scaling_y;
		scaling_y = q;
	}

	m_point_a.x = rect.left;
	m_point_a.y = rect.top;
	m_point_b.x = rect.right;
	m_point_b.y = rect.bottom;


	Display();
  }
}


int CDrawMethod::IsInsideField(CDPoint p)
{
  unsigned int lp;
  CDSize size;

	if (can_scale)
	{
		CRectTracker tracker( 
			CRect(static_cast<int>(m_point_a.x),
			static_cast<int>(m_point_a.y),
			static_cast<int>(m_point_b.x),
			static_cast<int>(m_point_b.y)), CRectTracker::dottedLine | CRectTracker::resizeOutside  );
		int r = tracker.HitTest( CPoint( static_cast<int>(p.x), static_cast<int>(p.y) ) );

		if (r != CRectTracker::hitNothing && r != CRectTracker::hitMiddle)
		{
			return r;
		}
	}

  // Convert co-ords to symbol space
  p.x = p.x -m_point_a.x;
  p.y = p.y -m_point_a.y;

  for (lp=0;lp < m_fields.size();lp++) 
  {
	if (!m_fields[lp].m_show)
	{
		continue;
	}

	size=m_pDesign->GetTextExtent(GetField(lp),fPIN);

	if (p.x>=m_fields[lp].m_position.x && p.x<=m_fields[lp].m_position.x+size.cx
	 && p.y<=m_fields[lp].m_position.y && p.y>=m_fields[lp].m_position.y-size.cy)
		break;
  }

  if (lp<m_fields.size())
	return lp + 1000;
  else
	return -1;
}


// The space between the method and text
#define SPACING 5


void CDrawMethod::NewRotation()
{
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );

  // Calculate the bounding box
  if ((rotate&3)<2)
	m_point_b=CDPoint(tr.x+m_point_a.x,tr.y+m_point_a.y);
  else
	m_point_b=CDPoint(tr.y+m_point_a.x,tr.x+m_point_a.y);

  // Calculate the text positions
  int TextHeight = - ListOfFonts::PIN_HEIGHT;
  int NextY		 = TextHeight;

  if (m_fields[Name].m_show) {
	if ((rotate&3)<2) {
		// Display to right hand side if display up or down
		m_fields[Name].m_position=CDPoint(SPACING,m_point_b.y+NextY-m_point_a.y);
		NextY+=TextHeight+SPACING/2;
	} else
		// Display above if left or right
		m_fields[Name].m_position=CDPoint(m_point_b.x-m_point_a.x,m_point_b.y-m_point_a.y);
  }

  CDPoint p;

	if ((rotate&3)<2)
	{
		// Display to right hand side if display up or down
		p=CDPoint(SPACING,m_point_b.y+NextY-m_point_a.y);
	}
	else
	{
		// Display below if left or right
		p=CDPoint(m_point_b.x-m_point_a.x,NextY);
	}

	// First place the shown fields
	unsigned int i;
	for (i = 0; i < m_fields.size(); i++)
	{
		// Don't do the name
		if (i==Name)
		{
			continue;
		}

		// Place the other fields
		if (m_fields[i].m_show) 
		{
			m_fields[i].m_position = p;
			p.y += TextHeight+SPACING/2;
		}
	}

	// Now place the hidden fields...
	for (i = 0; i < m_fields.size(); i++)
	{
		// Don't do the name
		if (i==Name)
		{
			continue;
		}

		// Place the other fields
		if (!m_fields[i].m_show && m_fields[i].m_type != always_hidden) 
		{
			m_fields[i].m_position = p;
			p.y += TextHeight+SPACING/2;
		}
	}
}

CDPoint CDrawMethod::GetTr()
{ 
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );
  return tr; 
}

void CDrawMethod::Display( BOOL erase )
{
	// Invalidate the symbol
	CDRect r( m_point_a.x,m_point_a.y,m_point_b.x,m_point_b.y);
	m_pDesign->InvalidateRect( r, erase, 10 );

  // Now invalidate our text
  for (unsigned int lp=0;lp < m_fields.size(); lp++)
  {
	if (m_fields[lp].m_show)
	{
		CDSize sz = m_pDesign->GetTextExtent( GetField(lp), fPIN );
		r.left = m_fields[lp].m_position.x+m_point_a.x;
		r.top = m_fields[lp].m_position.y+m_point_a.y;
		r.right = r.left + sz.cx;
		r.bottom = r.top - sz.cy;
		m_pDesign->InvalidateRect( r, erase, 5 );
	}
  }
}

void CDrawMethod::ScalePoint( CDPoint &r )
{
	r.x *= scaling_x;
	r.y *= scaling_y;
}

void CDrawMethod::Paint(CContext &dc,paint_options options)
{
  CDPoint tr;
  drawingCollection method;
  ExtractSymbol( tr, method );

  // Now rescale this device...
  double old_scaling_x;
  double old_scaling_y;
  dc.GetTransform().GetScaling( rotate, old_scaling_x, old_scaling_y );
  dc.SetScaling( rotate, scaling_x, scaling_y );

  // Move the object about by using the grid offset!
  // (that'll have to do until I think of a better method)
  CDPoint oldpos= dc.SetTRM(m_point_a,tr,rotate);


  drawingIterator it = method.begin();
  while (it != method.end()) 
  {
	(*it)->Paint(dc,options);
	++ it;
  }

  dc.SetScaling( rotate, old_scaling_x, old_scaling_y );
  dc.EndTRM(oldpos);

  // Now display the text (if necessary)
  dc.SelectFont(*m_pDesign->GetOptions()->GetFont(fPIN),2);
  switch (options)
  {
  case draw_selected:
	  dc.SetTextColor(m_pDesign->GetOptions()->GetUserColor().Get( CUserColor::PIN) );
	  break;
  case draw_selectable:
  	  dc.SetTextColor(cPIN_CLK);
	  break;
  default:
	  dc.SetTextColor(m_pDesign->GetOptions()->GetUserColor().Get( CUserColor::PIN));
  }

  for (unsigned int lp=0;lp < m_fields.size();lp++)
  {
	if (m_fields[lp].m_show)
		dc.TextOut(GetField(lp),
			CDPoint(m_fields[lp].m_position.x+m_point_a.x,
				m_fields[lp].m_position.y+m_point_a.y),options);
  }

}




// Store this method in the drawing
CDrawingObject* CDrawMethod::Store()
{
  CDrawMethod *NewObject;

  NewObject = new CDrawMethod(m_pDesign);

  *NewObject=*this;
  
  m_pDesign->Add(NewObject);

  return NewObject;
}



void CDrawMethod::PaintHandles( CContext &dc )
{
	if (can_scale)
	{
		// Put some handles around this object
		CDRect r(m_point_a.x,m_point_a.y,m_point_b.x,m_point_b.y);
		dc.PaintTracker( r );
	}
}

int CDrawMethod::SetCursorEdit( CDPoint p )
{
	CRect r(static_cast<int>(m_point_a.x),
		static_cast<int>(m_point_a.y),
		static_cast<int>(m_point_b.x),
		static_cast<int>(m_point_b.y) );
	r.NormalizeRect();
	CRectTracker tracker( r, CRectTracker::dottedLine | CRectTracker::resizeOutside  );
	int q = tracker.HitTest( CPoint( static_cast<int>(p.x), static_cast<int>(p.y) ) );

	if (q == 8 || !can_scale)
	{
		q = DistanceFromPoint( p ) <= 20;
		return q ? 11 : -1;
	}

	return q;
}


// Add/remove the next references
void CDrawMethod::AddReference( int min_ref, bool all_sheets )
{
	// Search the document for a gap in our references
	// 
	CString our_reference = GetSymbolData()->reference;
	int ppp = GetSymbolData()->ppp;

	int next_available_ref = min_ref;
	int last_same_symbol_ref = min_ref;
	int last_ppp_number = -1;
	bool found_same_symbol = false;
	bool alter_subpart = !GetSymbolData()->IsHeterogeneous() && ppp > 1;

	CMultiSheetDoc *pMultiDoc = m_pDesign->GetParent();
	int sheet = all_sheets ? 0 : pMultiDoc->GetActiveSheetIndex();
	do
	{
		bool our_sheet = pMultiDoc->GetSheet(sheet) == m_pDesign;
		drawingIterator it = pMultiDoc->GetSheet(sheet)->GetDrawingBegin();
		while (it != pMultiDoc->GetSheet(sheet)->GetDrawingEnd()) 
		{
			CDrawingObject *pointer = *it;
			CDrawMethod *pMethod = static_cast<CDrawMethod*>( pointer );

			if (   pointer->GetType() == xMethodEx3 
				&& pointer != this
				&& pMethod->HasRef()) 
			{
				// Has this got the same reference as us?
				if (pMethod->GetSymbolData()->reference == our_reference)
				{
					// This is the same reference, so we keep track
					// of the maximum value
					int ref_number = pMethod->GetRefVal();
					int ppp_number = pMethod->GetSubPart();


					// Ok, we must keep track of the next available reference
					if (next_available_ref <= ref_number)
					{
						next_available_ref = ref_number + 1;
					}

					// Is this the same symbol as us?
					if (pMethod->GetSymbolID() == GetSymbolID() && our_sheet)
					{
						found_same_symbol = true;

						if (    last_same_symbol_ref < ref_number
							|| (last_same_symbol_ref == ref_number && last_ppp_number < ppp_number) )
						{
							last_same_symbol_ref = ref_number;
							last_ppp_number = ppp_number;
						}
					}
				}
			}

			++ it;
		}
		++ sheet;
	} while ( all_sheets && sheet < pMultiDoc->GetNumberOfSheets() );

	// Now we know what the last value was, so set
	// us to the next value
	if (alter_subpart && found_same_symbol)
	{
		last_ppp_number ++;
		if (last_ppp_number >= ppp)
		{
			last_ppp_number = 0;
			last_same_symbol_ref = next_available_ref;
		}
		SetPart(last_ppp_number);
		SetRefVal(last_same_symbol_ref);
	}
	else
	{
		SetRefVal(next_available_ref);
		if (alter_subpart)
		{
			SetPart(0);
		}
	}
}

void CDrawMethod::RemoveReference()
{
	bool alter_subpart = !GetSymbolData()->IsHeterogeneous();

	SetRef(GetSymbolData()->reference);
	if (alter_subpart)
	{
		SetPart(0);
	}
}


// Extract the netlist/active points from this object
void CDrawMethod::GetActiveListFirst( CActiveNode &a )
{
	CDPoint tr;

	ExtractSymbol( tr, a.m_method );
	
	// Search the symbol for pins
	a.m_iterator = a.m_method.begin();
	while (a.m_iterator != a.m_method.end()) 
	{
		CDrawingObject *MethodPtr = (*a.m_iterator);
		CDrawPin* thePin = static_cast<CDrawPin*>((CDrawPin*)MethodPtr);

		// If it is a pin try and use it!
		if (MethodPtr->GetType()==xPinEx && !thePin->IsInvisible()) 
		{
			break;
		}

		++ a.m_iterator;
	}

}

bool CDrawMethod::GetActive( CActiveNode &a )
{
	// Search the symbol for pins
	while (a.m_iterator != a.m_method.end()) 
	{
		CDrawingObject *MethodPtr = (*a.m_iterator);
		CDrawPin* thePin = static_cast<CDrawPin*>((CDrawPin*)MethodPtr);

		// If it is a pin then use it
		if (MethodPtr->GetType()==xPinEx && !thePin->IsInvisible()) 
		{
			a.m_a = thePin->GetActivePoint( this );
			a.m_label = "";

			++ a.m_iterator;
			return true;
		}

		++ a.m_iterator;

	}

	return false;
}


