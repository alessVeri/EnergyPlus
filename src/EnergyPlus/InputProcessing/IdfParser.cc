// EnergyPlus, Copyright (c) 1996-2017, The Board of Trustees of the University of Illinois and
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy). All rights
// reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C++ Headers

// ObjexxFCL Headers

// EnergyPlus Headers
#include <InputProcessing/IdfParser.hh>
#include <InputProcessing/InputProcessor.hh>
#include <DataStringGlobals.hh>
#include <UtilityRoutines.hh>
#include <milo/dtoa.hpp>
#include <milo/itoa.hpp>

using json = nlohmann::json;

namespace EnergyPlus {

	json IdfParser::decode( std::string const & idf, json const & schema ) {
		bool success = true;
		return decode( idf, schema, success );
	}

	json IdfParser::decode( std::string const & idf, json const & schema, bool & success ) {
		success = true;
		if ( idf.empty() ) {
			success = false;
			return nullptr;
		}

		size_t index = 0;
		return parse_idf( idf, index, success, schema );
	}

	std::string IdfParser::encode( json const & root, json const & schema ) {
		std::string end_of_field( "," + DataStringGlobals::NL + "  " );
		std::string end_of_object( ";" + DataStringGlobals::NL + DataStringGlobals::NL );

		std::string encoded, extension_key;

		for ( auto obj = root.begin(); obj != root.end(); ++obj ) {
			const auto & legacy_idd = schema[ "properties" ][ obj.key() ][ "legacy_idd" ];
			const auto & legacy_idd_field = legacy_idd[ "fields" ];
			auto key = legacy_idd.find( "extension" );
			if ( key != legacy_idd.end() ) {
				extension_key = key.value().get< std::string >();
			}
			for ( auto obj_in = obj.value().begin(); obj_in != obj.value().end(); ++obj_in ) {
				encoded += obj.key();
				size_t skipped_fields = 0;
				for ( size_t i = 0; i < legacy_idd_field.size(); i++ ) {
					std::string const & entry = legacy_idd_field[ i ];
					if ( obj_in.value().find( entry ) == obj_in.value().end() ) {
						if ( entry == "name" ) encoded += end_of_field + obj_in.key();
						else skipped_fields++;
						continue;
					}
					for ( size_t j = 0; j < skipped_fields; j++ ) encoded += "," + DataStringGlobals::NL + "  ";
					skipped_fields = 0;
					encoded += end_of_field;
					auto const & val = obj_in.value()[ entry ];
					if ( val.is_string() ) {
						encoded += val.get < std::string >();
					} else {
						dtoa( val.get < double >(), s );
						encoded += s;
					}
				}

				if ( obj_in.value().find( extension_key ) == obj_in.value().end() ) {
					encoded += end_of_object;
					continue;
				}

				auto & extensions = obj_in.value()[ extension_key ];
				for ( size_t extension_i = 0; extension_i < extensions.size(); extension_i++ ) {
					auto const & cur_extension_obj = extensions[ extension_i ];
					auto const & extensible = schema[ "properties" ][ obj.key() ][ "legacy_idd" ][ "extensibles" ];
					for ( size_t i = 0; i < extensible.size(); i++ ) {
						std::string const & tmp = extensible[ i ];
						if ( cur_extension_obj.find( tmp ) == cur_extension_obj.end() ) {
							skipped_fields++;
							continue;
						}
						for ( size_t j = 0; j < skipped_fields; j++ ) encoded += end_of_field;
						skipped_fields = 0;
						encoded += end_of_field;
						if ( cur_extension_obj[ tmp ].is_string() ) {
							encoded += cur_extension_obj[ tmp ].get< std::string >();
						} else {
							dtoa( cur_extension_obj[ tmp ].get< double >(), s );
							encoded += s;
						}
					}
				}
				encoded += end_of_object;
			}
		}
		return encoded;
	}

	json IdfParser::parse_idf( std::string const & idf, size_t & index, bool & success, json const & schema ) {
		json root;
		Token token;
		auto const & schema_properties = schema[ "properties" ];

		while ( true ) {
			token = look_ahead( idf, index );
			if ( token == Token::END ) {
				break;
			} else if ( token == Token::NONE ) {
				success = false;
				return root;
			} else if ( token == Token::EXCLAMATION ) {
				eat_comment( idf, index );
			} else {
				std::string obj_name = parse_string( idf, index, success );

				auto const converted = InputProcessor::ConvertInsensitiveObjectType( obj_name );
				if ( converted.first ) {
					obj_name = converted.second;
				} else {
					print_out_line_error( idf, false );
					while ( token != Token::SEMICOLON && token != Token::END ) token = next_token( idf, index );
					continue;
				}

				json const & obj_loc = schema_properties[ obj_name ];
				json const & legacy_idd = obj_loc[ "legacy_idd" ];
				json obj = parse_object( idf, index, success, legacy_idd, obj_loc );
				if ( !success ) print_out_line_error( idf, true );
				u64toa( root[ obj_name ].size() + 1, s );
				std::string name = obj_name + " " + s;
				if ( !obj.is_null() ) {
					auto const & name_iter = obj.find( "name" );
					if ( name_iter != obj.end() ) {
						name = name_iter.value();
						obj.erase( name_iter );
						if ( root[ obj_name ].find( name ) != root[ obj_name ].end() ) {
							// hacky but needed to warn if there are duplicate names in parsed IDF
							if ( obj_name != "RunPeriod" ) {
								ShowWarningMessage("Duplicate name found. name: \"" + name + "\"");
							}
						}
					}
				}

				root[ obj_name ][ name ] = std::move( obj );
			}
		}

		return root;
	}

	json IdfParser::parse_object( std::string const & idf, size_t & index, bool & success,
								  json const & legacy_idd, json const & schema_obj_loc ) {
		json root = json::object();
		json extensible = json::object();
		json array_of_extensions = json::array();
		Token token;
		std::string extension_key;
		size_t legacy_idd_index = 0, extensible_index = 0;
		success = true;
		bool was_value_parsed = false;
		auto const & legacy_idd_fields_array = legacy_idd[ "fields" ];
		auto const & legacy_idd_extensibles_iter = legacy_idd.find( "extensibles" );

		auto const & schema_patternProperties = schema_obj_loc[ "patternProperties" ];
		auto const & schema_dot_star = schema_patternProperties[ ".*" ];
		auto const & schema_obj_props = schema_dot_star[ "properties" ];
		auto key = legacy_idd.find( "extension" );

		json const * schema_obj_extensions = nullptr;
		if ( legacy_idd_extensibles_iter != legacy_idd.end() ) {
			extension_key = key.value();
			schema_obj_extensions = & schema_obj_props[ extension_key ][ "items" ][ "properties" ];
		}

		auto const & found_min_fields = schema_obj_loc.find( "min_fields" );

		index += 1;

		while ( true ) {
			token = look_ahead( idf, index );
			if ( token == Token::NONE ) {
				success = false;
				return root;
			} else if ( token == Token::END ) {
				return root;
			} else if ( token == Token::COMMA || token == Token::SEMICOLON ) {
				if ( !was_value_parsed ) {
					int ext_size = 0;
					if ( legacy_idd_index < legacy_idd_fields_array.size() ) {
						std::string const & field_name = legacy_idd_fields_array[ legacy_idd_index ];
						root[ field_name ] = "";
					} else {
						auto const & legacy_idd_extensibles_array = legacy_idd_extensibles_iter.value();
						ext_size = static_cast<int>( legacy_idd_extensibles_array.size() );
						std::string const & field_name = legacy_idd_extensibles_array[ extensible_index % ext_size ];
						extensible_index++;
						extensible[ field_name ] = "";
					}
					if ( ext_size && extensible_index % ext_size == 0 ) {
						array_of_extensions.push_back( extensible );
						extensible.clear();
					}
				}
				legacy_idd_index++;
				was_value_parsed = false;
				next_token( idf, index );
				if ( token == Token::SEMICOLON ) {
					size_t min_fields = 0;
					if ( found_min_fields != schema_obj_loc.end() ) {
						min_fields = found_min_fields.value();
					}
					for (; legacy_idd_index < min_fields; legacy_idd_index++) {
						std::string const & field_name = legacy_idd_fields_array[ legacy_idd_index ];
						root[ field_name ] = "";
					}
					if ( extensible.size() ) {
						array_of_extensions.push_back( extensible );
						extensible.clear();
					}
					break;
				}
			} else if ( token == Token::EXCLAMATION ) {
				eat_comment( idf, index );
			} else if ( legacy_idd_index >= legacy_idd_fields_array.size() ) {
				if ( legacy_idd_extensibles_iter == legacy_idd.end() ) {
					success = false;
					return root;
				}
				auto const & legacy_idd_extensibles_array = legacy_idd_extensibles_iter.value();
				auto const size = legacy_idd_extensibles_array.size();
				std::string const & field_name = legacy_idd_extensibles_array[ extensible_index % size ];
				auto const val = parse_value( idf, index, success, schema_obj_extensions->at( field_name ) );
				extensible[ field_name ] = std::move( val );
				was_value_parsed = true;
				extensible_index++;
				if ( extensible_index && extensible_index % size == 0 ) {
					array_of_extensions.push_back( extensible );
					extensible.clear();
				}
			} else {
				was_value_parsed = true;
				std::string const & field = legacy_idd_fields_array[ legacy_idd_index ];
				auto const & find_field_iter = schema_obj_props.find( field );
				if ( find_field_iter == schema_obj_props.end() ) {
					if ( field == "name" ) {
						root[ field ] = parse_string( idf, index, success );
					} else {
						u64toa( cur_line_num, s );
						ShowWarningMessage( "Field " + field + " was not found at line " + s );
					}
				} else {
					auto const val = parse_value( idf, index, success, find_field_iter.value() );
					root[ field ] = std::move( val );
				}
				if ( !success ) return root;
			}
		}
		if ( array_of_extensions.size() ) {
			root[ extension_key ] = std::move( array_of_extensions );
			array_of_extensions = nullptr;
		}
		return root;
	}

	json IdfParser::parse_number( std::string const & idf, size_t & index, bool & success ) {
		size_t save_i = index;
		eat_whitespace( idf, save_i );
		bool is_double = false, is_sign = false, is_scientific = false;
		std::string num_str, numeric = "-+.eE0123456789";

		while ( numeric.find_first_of( idf[ save_i ] ) != std::string::npos ) {
			num_str += idf[ save_i ];
			if ( idf[ save_i ] == '.' ) {
				if ( is_double ) {
					success = false;
					return nullptr;
				}
				is_double = true;
			} else if ( idf[ save_i ] == '-' || idf[ save_i ] == '+' ) {
				if ( is_sign && !is_scientific ) {
					success = false;
					return nullptr;
				}
				is_sign = true;
			} else if ( idf[ save_i ] == 'e' || idf[ save_i ] == 'E' ) {
				if ( is_scientific ) {
					success = false;
					return nullptr;
				}
				is_scientific = true;
				is_double = true;
			}
			save_i++;
		}


		assert( !num_str.empty() );

		if ( num_str[ num_str.size() - 1 ] == 'e' || num_str[ num_str.size() - 1 ] == 'E' ) {
			success = false;
			return nullptr;
		}

		Token token = look_ahead( idf, save_i );
		if ( token != Token::SEMICOLON && token != Token::COMMA ) {
			success = false;
			return nullptr;
		}
		json val;
		if ( is_double ) {
			try {
				auto const double_val = stod(num_str, nullptr );
				val = double_val;
			} catch ( std::exception e ) {
				auto const double_val = stold( num_str, nullptr );
				val = double_val;
			}
		} else {
			try {
				auto const int_val = stoi( num_str, nullptr );
				val = int_val;
			} catch ( std::exception e ) {
				auto const int_val = stoll( num_str, nullptr );
				val = int_val;
			}
		}
		index = save_i;
		return val;
	}

	json IdfParser::parse_value( std::string const & idf, size_t & index, bool & success, json const & field_loc ) {
		auto const & field_type = field_loc.find("type");
		if ( field_type != field_loc.end() ) {
			if ( field_type.value() == "number" || field_type.value() == "integer" ) {
				return parse_number( idf, index, success );
			} else {
				auto const parsed_string = parse_string( idf, index, success );
				auto const & enum_it = field_loc.find( "enum" );
				if ( enum_it == field_loc.end() ) return parsed_string;
				for ( auto const & s : enum_it.value() ) {
					auto const & str = s.get < std::string >();
					if ( UtilityRoutines::SameString( str, parsed_string ) ) {
						return str;
					}
				}
				return parsed_string;
			}
		} else {
			switch (look_ahead(idf, index)) {
				case Token::STRING: {
					auto const parsed_string = parse_string(idf, index, success);
					auto const & enum_it = field_loc.find( "enum" );
					if (enum_it != field_loc.end()) {
						for ( auto const & s : enum_it.value() ) {
							auto const & str = s.get < std::string >();
							if ( UtilityRoutines::SameString( str, parsed_string ) ) {
								return str;
							}
						}
					} else if ( UtilityRoutines::SameString( parsed_string, "Autosize" ) || UtilityRoutines::SameString( parsed_string, "Autocalculate" ) ) {
						auto const & default_it = field_loc.find( "default" );
						// The following is hacky because it abuses knowing the consistent generated structure
						// in the future this might not hold true for the array indexes.
						if ( default_it != field_loc.end() ) {
							return field_loc[ "anyOf" ][ 1 ][ "enum" ][ 1 ];
						} else {
							return field_loc[ "anyOf" ][ 1 ][ "enum" ][ 0 ];
						}
					}
					return parsed_string;
				}
				case Token::NUMBER: {
					size_t save_line_index = index_into_cur_line;
					size_t save_line_num = cur_line_num;
					json value = parse_number(idf, index, success);
					if ( !success ) {
						cur_line_num = save_line_num;
						index_into_cur_line = save_line_index;
						success = true;
						return parse_string(idf, index, success);
					}
					return value;
				}
				case Token::NONE:
				case Token::END:
				case Token::EXCLAMATION:
				case Token::COMMA:
				case Token::SEMICOLON:
					break;
			}
			success = false;
			return nullptr;
		}
	}

	std::string IdfParser::parse_string( std::string const & idf, size_t & index, bool & success ) {
		eat_whitespace( idf, index );

		std::string s;
		char c;

		bool complete = false;
		while ( !complete ) {
			if ( index == idf.size() ) {
				complete = true;
				break;
			}

			c = idf[ index ];
			increment_both_index( index, index_into_cur_line );
			if ( c == ',' ) {
				complete = true;
				decrement_both_index( index, index_into_cur_line );
				break;
			} else if ( c == ';' ) {
				complete = true;
				decrement_both_index( index, index_into_cur_line );
				break;
			} else if ( c == '!' ) {
				complete = true;
				decrement_both_index( index, index_into_cur_line );
				break;
			} else if ( c == '\\' ) {
				if ( index == idf.size() ) break;
				char next_c = idf[ index ];
				increment_both_index( index, index_into_cur_line );
				if ( next_c == '"' ) {
					s += '"';
				} else if ( next_c == '\\' ) {
					s += '\\';
				} else if ( next_c == '/' ) {
					s += '/';
				} else if ( next_c == 'b' ) {
					s += '\b';
				} else if ( next_c == 't' ) {
					s += '\t';
				} else if ( next_c == 'n' ) {
					complete = false;
					break;
				} else if ( next_c == 'r' ) {
					complete = false;
					break;
				} else {
					s += c;
					s += next_c;
				}
			} else {
				s += c;
			}
		}

		if ( !complete ) {
			success = false;
			return std::string();
		}
		return rtrim( s );
	}

	void IdfParser::increment_both_index( size_t & index, size_t & line_index ) {
		index++;
		line_index++;
	}

	void IdfParser::decrement_both_index( size_t & index, size_t & line_index ) {
		index--;
		line_index--;
	}

	void IdfParser::print_out_line_error( std::string const & idf, bool obj_found ) {
		std::string line;
		if ( obj_found ) ShowWarningError( "error: \"extra field(s)\" " );
		else ShowWarningError( "error: \"obj not found in schema\" " );
		ShowWarningError( "at line number " + std::to_string( cur_line_num )
									  + " (index " + std::to_string(index_into_cur_line)  + ")\nLine:\n" );
		while ( idf[ beginning_of_line_index++ ] != '\n' ) line += idf[ beginning_of_line_index ];
		ShowWarningError( line );
	}

	void IdfParser::eat_whitespace( std::string const & idf, size_t & index ) {
		while ( index < idf.size() ) {
			switch ( idf[ index ] ) {
				case ' ':
				case '\r':
				case '\t':
					increment_both_index( index, index_into_cur_line );
					continue;
				case '\n':
					increment_both_index( index, cur_line_num );
					beginning_of_line_index = index;
					index_into_cur_line = 0;
					continue;
				default:
					return;
			}
		}
	}

	void IdfParser::eat_comment( std::string const & idf, size_t & index ) {
		while ( true ) {
			if ( index == idf.size() ) break;
			if ( idf[ index ] == '\n' ) {
				increment_both_index( index, cur_line_num );
				index_into_cur_line = 0;
				beginning_of_line_index = index;
				break;
			}
			increment_both_index( index, index_into_cur_line );
		}
	}

	IdfParser::Token IdfParser::look_ahead( std::string const & idf, size_t index ) {
		size_t save_index = index;
		size_t save_line_num = cur_line_num;
		size_t save_line_index = index_into_cur_line;
		Token token = next_token( idf, save_index );
		cur_line_num = save_line_num;
		index_into_cur_line = save_line_index;
		return token;
	}

	IdfParser::Token IdfParser::next_token( std::string const & idf, size_t & index ) {
		eat_whitespace( idf, index );

		if ( index == idf.size() ) {
			return Token::END;
		}

		char const c = idf[ index ];
		increment_both_index( index, index_into_cur_line );
		switch ( c ) {
			case '!':
				return Token::EXCLAMATION;
			case ',':
				return Token::COMMA;
			case ';':
				return Token::SEMICOLON;
			default:
				static std::string const search_chars( "-:.#/\\[]{}_@$%^&*()|+=<>?'\"~" );
				static std::string const numeric( ".-+0123456789" );
				if ( numeric.find_first_of( c ) != std::string::npos ) {
					return Token::NUMBER;
				} else if ( isalnum( c ) || ( std::string::npos != search_chars.find_first_of( c ) ) ) {
					return Token::STRING;
				}
				break;
		}
		decrement_both_index( index, index_into_cur_line );
		return Token::NONE;
	}

}