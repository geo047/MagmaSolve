/*!
 *  CStringManipulation.h
 */
#include <string>
#include <algorithm>

class CStringManipulation 
{
public: 
  int    length  	     ( std::string   tStr ) ;
  // Returns the left hand side of a std::string of format :  "left hand side = right hand side"  returns "left hand side"
  std::string LeftSideOfEqual(std::string tStr) ;
  // Returns the left hand side of a std::string of format :  "left hand side = right hand side"  returns "left hand side"
  std::string LeftSideOfEqual(std::string tStr, bool forceLowerCase) ; 
  // Returns the left hand side of a std::string of format :  "left hand side inChar right hand side"  returns "left hand side "
  std::string LeftSideOfString(std::string inChar, std::string tStr, bool forceLowerCase) ; // returns the left hand side std::string before first occurence of inChar 
  // returns the right hand side of a std::string (in lower case) of format :  "left hand side = right hand side"  returns "right hand side"
  std::string RightSideOfString(std::string inChar, std::string tStr, bool forceLowerCase) ; // returns the riight hand side std::string after first occurence of inChar  
  // returns the right hand side of a std::string (in lower case) of format :  "left hand side = right hand side"  returns "right hand side"
  std::string RightSideOfEqual(std::string tStr, bool forceLowerCase) ;
  // converts upper case characters in tStr to lower case
  std::string lowerCase(std::string tStr) ;
  // removes leading whitespace
  std::string trim( std::string tStr ) ;

} ;