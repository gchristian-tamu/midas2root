// // M2N
// #include "midas2root.h"
//STL
#include <fstream>
#include <Riostream.h>
#include <Byteswap.h>
#include <bitset>
#include <memory>
#include<map>
#include<vector>
#include<string>

//ROOT
#include "TRandom.h"
#include "TString.h"
#include"TFile.h"
#include"TTree.h"

///////////////////////////// HEADER PART /////////////////////////////////

// Define the size of long and int, needed to set correctly the header size
// Work for GCC need test on Mac OS
#if __GNUC__
#if __x86_64__ || __ppc64__
#define M2R_64SYSTEM 
#else
#define M2R_32SYSTEM
#endif
#endif

namespace m2r{

// MIDAS file data block header
struct s_data_header {
	char*   header_id = new char[9];     //   contains the string  EBYEDATA
#ifdef M2R_64SYSTEM
	int header_sequence;  //   within the file
#else
	long header_sequence; //   within the file
#endif
	short int header_stream;    //   data acquisition stream number (in the range 1=>4)
	short int header_tape;      //   =1
	short int header_MyEndian;  //   written as a native 1 by the tape server
	short int header_DataEndian; //   written as a native 1 in the hardware structure of the data following
#ifdef M2R_64SYSTEM
	int header_dataLen;   //   total length of useful data following the header in bytes
#else
	long header_dataLen;  //   total length of useful data following the header in bytes
#endif
}; 
typedef struct s_data_header DATA_HEADER;


class MidasInput{
public: // Constructor
	MidasInput();
	~MidasInput();

public: // Setter
	void ReadChannelMap(const std::string& filename);
	void SetFileName(const std::string& filename); 
	void SetOutputFile(const std::string& filename);
	void SetADCBaseOffset(int base,int offset) {m_ADCBase=base;m_ADCOffset=offset;}
			
public: // Heavy work
	void TreatFile();
	void ClearEvent();
	void FillHit(int address, unsigned short int value);
	void CloseOutputFile();
	bool ReadBlockHeader(ifstream& fin);
	bool ReadBlock(ifstream& fin);
	unsigned short ReadWord(ifstream& fin);
	unsigned short ReadMiniWord(ifstream& fin);

	unsigned short BitMask(unsigned short a, unsigned short b);
	unsigned short Swap(unsigned short value);
	int ADCChannelToAddress(int ADC, int Channel){
		return ((ADC-1)*m_ADCBase+Channel+m_ADCOffset);
	}

public: // Generate tag to simulate a TreatFile 
	void SimulateTreat(int event, int cmin=0 , int cmax=65536, int vmin=0 , int vmax=65536);

private:
	TFile* m_File;
	TTree* m_Tree;
	DATA_HEADER m_header;
	std::string m_FileName;
	int m_ADCBase;
	int m_ADCOffset;
	std::map<int, std::vector<double>* > m_ChannelMap;
	unsigned int m_blocks;
	unsigned int m_events;
	unsigned int m_bytes_read;
};
}

////////////////////////////////////////////////////////////////////////////////

/////// CODE PART///////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
m2r::MidasInput::MidasInput(){
  m_FileName="not-set";
	m_File=0;
  m_Tree=0;
  m_blocks = 0; 
  m_events = 0;
	m_ADCBase   = -1;
	m_ADCOffset = -1;
  // Check the header size is correct:
  m2r::DATA_HEADER block;
  double header_size = sizeof(block.header_id)+
    sizeof(block.header_sequence)+
    sizeof(block.header_stream)+
    sizeof(block.header_tape)+
    sizeof(block.header_MyEndian)+
    sizeof(block.header_DataEndian)+
    sizeof(block.header_dataLen);

  if(header_size!=24){
    cout << "ERROR: The header block size is " << header_size << "instead of 24 bytes " << endl;
    exit(1);
  }
}
////////////////////////////////////////////////////////////////////////////////
m2r::MidasInput::~MidasInput(){
//	if (m_File) {m_File->Close(); m_File = 0;}
}
void m2r::MidasInput::CloseOutputFile(){
	if (m_File) {m_File->Close();}
}
////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::ClearEvent(){
	auto doClear = [](pair<const int, vector<double>* > &p)
		{ p.second->clear(); };
	for_each(m_ChannelMap.begin(),m_ChannelMap.end(),
					 doClear);
}
////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::ReadChannelMap(const std::string& filename){
	if(!m_ChannelMap.empty()){ m_ChannelMap.clear(); }
	if(!m_Tree){
		cerr<<"ERROR(midas2root.cxx): Must call SetFileName() before ReadChannelMap\n";
		exit(1);
	}
	if(m_ADCBase < 0 ||m_ADCOffset < 0){
		cerr<<"ERROR(midas2root.cxx): Must call SetADCBaseOffset() before ReadChannelMap\n";
		exit(1);
	}
	auto tokenizeLine = [](const string& line){
		unique_ptr<TObjArray> tokens ( TString(line).Tokenize(",") );
		tokens->SetOwner(true); // required for auto-deleting elements
		vector<string> out(tokens->GetEntries());
		for(int i=0;i<tokens->GetEntries();++i){
			out[i]=static_cast<TObjString*>(tokens->At(i))->GetString().Data();
		}
		return out;
	};
	string line;
	ifstream ifs(filename.c_str());
	int linenum = 0;
	std::getline(ifs,line);++linenum; // header - Ignore
	while(std::getline(ifs,line)){
		vector<string> elements = tokenizeLine(line);
		if(elements.size() != 3){
			cerr << "ERROR (midas2root.cxx): Bad line at line no. " << linenum <<
				" in file " << filename << ", skipping...\n";
		}
		int address = ADCChannelToAddress(std::atoi(elements[1].c_str()),std::atoi(elements[2].c_str()));
		if(m_ChannelMap.find(address) != m_ChannelMap.end()){
			cerr << "WARNING (midas2root.cxx): Duplicate entry at line " << linenum <<
				" in file " << filename << ", ignoring and keeping the EARLIER one...";
		} else {
			vector<double> *vb=0;
			m_Tree->Branch(elements[0].c_str(),&vb);
			auto emp = m_ChannelMap.emplace(address, vb);
			assert(emp.second);
			cout << "Created branch << \"" << elements[0] << "\" for (ADC,CHANNEL,ADDRESS) = ("
					 << elements[1] << ", " << elements[2] << ", " << address << ")...\n";
		}
		++linenum;
	};
}
////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::FillHit(int address, unsigned short int value){
	auto it = m_ChannelMap.find(address);
	if(it != m_ChannelMap.end()){
		it->second->push_back(value);
	} else {
		cerr << "ERROR (midas2root.cxx): Found un-mapped channel at address: " <<
			address << ", skipping...\n";
	}
}
////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::SetOutputFile(const std::string& filename){
	if(m_File) { m_File->Delete(); }
	m_File = new TFile(filename.c_str(),"RECREATE");
	m_Tree = new TTree("MidasTree","Unpacked midas tree");
	cout << "Successfully created ROOT tree \"MidasTree\" in ROOT file << \""
			 << filename << "\"...\n";
}
////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::SetFileName(const string& filename){
  m_FileName=filename;
}
////////////////////////////////////////////////////////////////////////////////
bool m2r::MidasInput::ReadBlockHeader(ifstream& fin){
  m_bytes_read = 0;
  strcpy(m_header.header_id,"        \0");
  static char buffer1[5];
  static char buffer2[5];
  strcpy(buffer1,"    \0");
  strcpy(buffer2,"    \0");
  while(strcmp(m_header.header_id,"EBYEDATA")!=0 && !fin.eof()){     
    // there is some padding at the end of the block to be ignored
    // because a block is always 16K but an event cannot by split on two blocks
    fin.read(buffer1,4);
    if(strcmp(buffer1,"EBYE")==0){
      fin.read(buffer2,4);
      for(unsigned int i = 0 ; i < 4 ;i++)
        m_header.header_id[i] = buffer1[i];
      for(unsigned int i = 0 ; i < 4 ;i++)
        m_header.header_id[i+4] = buffer2[i];
    }

  } 
  if(fin.eof()) 
    return false;
  else{
    m_blocks++;
    fin.read((char*)&m_header.header_sequence,sizeof(m_header.header_sequence));
    fin.read((char*)&m_header.header_stream,sizeof(m_header.header_stream));
    fin.read((char*)&m_header.header_tape,sizeof(m_header.header_tape));
    fin.read((char*)&m_header.header_MyEndian,sizeof(m_header.header_MyEndian));
    fin.read((char*)&m_header.header_DataEndian,sizeof(m_header.header_DataEndian));
    fin.read((char*)&m_header.header_dataLen,sizeof(m_header.header_dataLen));
    return true;
  }
}
////////////////////////////////////////////////////////////////////////////////
unsigned short m2r::MidasInput::Swap(unsigned short value){
  bitset<16> bsvalue(value);
  bitset<16> bsword(0);
  for(unsigned int i = 0 ; i < 16 ; i++){
    bsword[15-i] = bsvalue[i];
  }

  unsigned short word = bsword.to_ulong();
  return word;
}


////////////////////////////////////////////////////////////////////////////////
unsigned short m2r::MidasInput::ReadWord(ifstream& fin){
  m_bytes_read+=2;
  unsigned short word = 0; 
  fin.read((char*)&word,sizeof(word));
  if(m_header.header_DataEndian){
    word = Rbswap_16(word);
    word = Swap(word); 
  }
  return word;
}
////////////////////////////////////////////////////////////////////////////////
unsigned short m2r::MidasInput::ReadMiniWord(ifstream& fin){
  m_bytes_read+=1;
  unsigned short word = 0; 
  fin.read((char*)&word,1);
  return word;
}
////////////////////////////////////////////////////////////////////////////////
unsigned short m2r::MidasInput::BitMask(unsigned short a, unsigned short b){
  unsigned short r = 0;
  for (unsigned i=a; i<=b; i++)
    r |= 1 << i;
  return r;
}
////////////////////////////////////////////////////////////////////////////////
bool m2r::MidasInput::ReadBlock(ifstream& fin){
  // Read the block header
  if(!ReadBlockHeader(fin)){
    return false;
  }

  // Read the block 
  unsigned short int whole;
  unsigned short int address;
  unsigned short int item;
  unsigned short int value;
  unsigned short int control;
  unsigned short int count;
  unsigned short int group;
  unsigned short int min;
  unsigned short int max;
  while(1){
    // read the first word
    whole = ReadWord(fin);
    unsigned short int ctrl = whole & BitMask(14,15) >> 14;
    if(ctrl == 3){ // begin of event or end of block
      unsigned short int check1 = (whole & BitMask(8,13))>>8;
      unsigned short int check2 = (whole & BitMask(0,7));

      value = ReadWord(fin);
      if(check1==0x3f && check2==0xff){
        if(value == 0){ // end of block (end of current event)
          return true;
        }
        else{ // begin of event (end of previous event)
          m_Tree->Fill();
          ClearEvent(); //m_DetectorManager->Clear();
          if(m_events++%5000 ==0)
            cout << "\r Blocks treated: " << m_blocks << "\t Event treated: " << m_events;
        }
      }
      else{
        cout << "Shit Happen!" << endl;
      }
    }

    else if(ctrl == 0){ // Simple data word
      value = ReadWord(fin);
      address = Swap(whole);
      group=address & 0x00ff;
      item=address >> 8 & 0x003f;
      address = 32 * (group - 1) + item;

      FillHit(address,Swap(value)); // m_DetectorManager->Fill(address,Swap(value));
    }

    else if(ctrl == 1){ // Group data item
      count = (whole & BitMask(8,13));
      group = (whole & BitMask(0,7));
      for(unsigned int i = 0 ; i < count ; i++){
        value = ReadWord(fin);
      }
    }
    else if(ctrl == 2){ // Extended group data item
      count = (whole & BitMask(0,13));
      group = ReadWord(fin);
      if(count == 0){
        min = ReadWord(fin);
        max = ReadWord(fin);
        count = max - min;
      }

      for(unsigned int i = 0 ; i < count ; i++){
        value = ReadWord(fin);
      } 
    }
    else
      cout << "Control shit happen : " << ctrl << endl;
  }
}

////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::TreatFile(){

  
  // Minor adaptation of Raw_Tree.C macro from University of York group 
  ifstream fin;
  fin.open(m_FileName,ifstream::binary);
  if(!fin.is_open()){
    cout << "ERROR: fail to open file " << m_FileName << endl;
    exit(1);
  }
  else{
    cout << "**** Treating MIDAS file " << m_FileName << " ****" << endl;
    cout << "\r------------------------Starting Process-------------------------" << endl;
  }
  while(ReadBlock(fin)){
    // Read all the block. ReadBlock return false on eof
  }
  cout << "\r----------------------Processed All Blocks-----------------------" << endl;
  cout << "Treated blocks: " << m_blocks << "\t Treated Events: " << m_events << endl;

	m_Tree->Write();
  fin.close();
}
////////////////////////////////////////////////////////////////////////////////
void m2r::MidasInput::SimulateTreat(int event, int cmin , int cmax, int vmin,int vmax){
  unsigned int count=0;
  cout << "Starting Simulation " << endl;
  TRandom rand;

  while( count++ < event){
    int channel;
    int value;
    ClearEvent();//m_DetectorManager->Clear();
    if(count%500==0)
      cout <<"\r **** Simulating treat : " << count*100.0/event  << "% ****" ;
    // Random channel
    channel = rand.Uniform(cmin,cmax);

    // Random value
    value = rand.Uniform(vmin,vmax);
    FillHit(channel,value);//m_DetectorManager->Fill(channel,value);
    m_Tree->Fill();

  }
  cout << endl;
}

void midas2root(const string& midas_file,
								const string& root_file,
								const string& channel_map_file,
								int adc_base = 32, int adc_offset = 992)
{
	m2r::MidasInput mi;
	mi.SetFileName(midas_file);
	mi.SetOutputFile(root_file);
	mi.SetADCBaseOffset(adc_base,adc_offset);
	mi.ReadChannelMap(channel_map_file);
	mi.TreatFile();
	mi.CloseOutputFile();
}
int main(){return 0;}
