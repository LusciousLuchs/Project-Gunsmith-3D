// Gunsmith3D - pieces: a part with its welded piece and the joints between them, as text. Copy and paste (Ctrl+C,
// Ctrl+V, through the system clipboard, so between guns and between copies of the game), and the parts library
// (user/parts): your own parts and assemblies, saved once and dropped into any gun.
#pragma once
#include <string>
#include <vector>

std::string pieceText(int bodyIndex);                 // the part, its welded piece, and the joints among them
bool insertPiece(const std::string& text, double dx, double dy);  // added to the design as new parts (new names inside), selected
void copySelection();
void pasteClipboard();
bool savePieceToLibrary(int bodyIndex, const std::string& name);
std::vector<std::string> libraryParts();              // names in user/parts
bool insertLibraryPart(const std::string& name);
