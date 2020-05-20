#pragma once
#include <cassert>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "IDatabase.h"
#include "sqlite3.h"


class DataBase
{
	sqlite3* m_db{};

public:
	template<class ...Ts>
	class RecordRange
	{
		sqlite3* m_db{};
		std::string m_sql;

	public:
		template<class... Ts>
		class Iterator
		{
			const int m_column_count = sizeof...(Ts);
			using ValueType = std::variant<Ts...>;
			using RecordType = std::vector<ValueType>;

			sqlite3_stmt* m_statement{};
			int m_status{ SQLITE_DONE };
			int m_column_index{ -1 };

			const size_t m_hash{};

		public:
			explicit Iterator(std::string sql, sqlite3* db, size_t hash) : m_status{ ::sqlite3_prepare_v2(db, sql.c_str(), -1, &m_statement, {}) }, m_hash{ hash }
			{
				if (m_status == SQLITE_OK) {
					m_status = ::sqlite3_step(m_statement);
				} else {
					auto message = sqlite3_errmsg( db );

					throw std::string( message );
				}
			}

			explicit Iterator(std::string sql, size_t hash) : m_hash{ hash }, m_status{ SQLITE_DONE }
			{}

			~Iterator()
			{
				m_status = ::sqlite3_finalize( m_statement );
			}

			Iterator& operator++()
			{
				/*
				http://wiki.pchero21.com/wiki/Libsqlite3

				SQLITE_BUSY
				현재 데이터베이스에 lock 이 걸려 있음을 뜻한다. 다시 재시도를 하거나 롤백 후, 다시 시도할 수 있다.
				SQLITE_DONE
				해당 쿼리 구문이 성공적으로 수행(종료)되었음을 의미한다. sqlite3_reset() 호출 후, sqlite3_step()를 수행하여야 한다.
				SQLITE_ROW
				수행 결과에 데이터가 있음을 나타낸다. select 구문과 같은 경우를 생각해볼 수 있는데, 이후 column_access 함수들을 사용할 수 있다.
				SQLITE_ERROR
				오류가 발생했음을 나타낸다.
				SQLITE_MISUSE
				잘못된 사용법 오류.
				*/
				m_status = ::sqlite3_step( m_statement );

				return *this;
			}

			RecordType operator*()
			{
				if (m_status == SQLITE_ROW) {
					return { _get_column<Ts>(m_statement)... };
				}
				else {
					assert(m_status == SQLITE_DONE);
					return {};
				}
			}

			bool operator==( const Iterator& lhs ) const
			{
				assert(lhs.m_hash == m_hash);

				return lhs.m_status == m_status;
			}

			bool operator!=( const Iterator& lhs ) const
			{
				return !operator==( lhs );
			}

		private:
			template<class T>
			ValueType _get_column(sqlite3_stmt* statement)
			{
				static_assert(false, "not implemented type");

				return {};
			}

			template<>
			ValueType _get_column<int>(sqlite3_stmt* statement)
			{
				auto column = _get_column_index();

				return ::sqlite3_column_int(m_statement, column);
			}

			template<>
			ValueType _get_column<std::string>(sqlite3_stmt* statement)
			{
				auto column = _get_column_index();
				auto text{ ::sqlite3_column_text(m_statement, column) };
				auto text_{ reinterpret_cast<const char*>(text) };

				return std::string{ text_ };
			}

			int _get_column_index()
			{
				return ++m_column_index % m_column_count;
			}

			template<class T>
			size_t _get_hash(T t)
			{
				return std::hash<T>{}(t);
			}
		};

	public:
		RecordRange( sqlite3* db, std::string sql ) : m_db{ db }, m_sql{ sql }
		{}

		auto begin()
		{
			static_assert(sizeof(this) == sizeof(size_t), "invalid size");

			return Iterator<Ts...>{ m_sql, m_db, reinterpret_cast<size_t>(this) };
		}

		auto end()
		{
			static_assert(sizeof(this) == sizeof(size_t), "invalid size");

			return Iterator<Ts...>{ m_sql, reinterpret_cast<size_t>(this) };
		}
	};

	DataBase()
	{
		auto result = ::sqlite3_open( "tetris.db", &m_db );

		if (SQLITE_OK == result) {
			_initialize_table();
		}
		else {
			throw std::string("db open failed");
		}
	}

	virtual ~DataBase()
	{
		auto result = ::sqlite3_close( m_db );

		assert(SQLITE_OK == result);
	}

	/*
	get range for iterator. you can use such style:

	0th elem: name
	1st elem: score
	*/
	auto get_ranks( size_t size )
	{
		auto sql = "SELECT NAME, SCORE FROM RANK ORDER BY SCORE";

		return _get_record_range<std::string, int>(sql);
	}

	bool put_rank(const std::string& name, int score)
	{
		std::ostringstream stream;
		stream << "INSERT INTO RANK (NAME, SCORE) VALUES ('" << name << "', " << score << ");";

		char* message_error{};
		auto result = sqlite3_exec(m_db, stream.str().c_str(), {}, {}, &message_error);

		if (result == SQLITE_OK) {
			return true;
		}
		else {
			std::string message_error_{ message_error };
			::sqlite3_free(message_error);

			std::cerr << message_error << std::endl;

			return {};
		}
	}

private:
	template<class ...Ts>
	auto _get_record_range(std::string sql)
	{
		return RecordRange<Ts...>{m_db, sql};
	}

	void _initialize_table()
	{
		constexpr auto table_name = "RANK";
		std::ostringstream stream;
		stream << "SELECT name FROM sqlite_master WHERE type = 'table' AND name = '" << table_name << "';";

		auto records = _get_record_range<std::string>(stream.str());
		auto record_it = std::begin(records);
		auto record = *record_it;
		
		if (record.empty()) {
			auto sql = \
				"CREATE TABLE RANK( \
					ID INTEGER PRIMARY KEY AUTOINCREMENT, \
					NAME TEXT NOT NULL, \
					SCORE INT NOT NULL \
				)";
			char* message_error{};
			auto result = ::sqlite3_exec(m_db, sql, {}, {}, &message_error);

			if (result == SQLITE_OK) {
				std::cout << "table created: " << table_name << std::endl;
			} else {
				std::string message_error_{ message_error };
				::sqlite3_free(message_error);

				throw message_error_;
			}
		}
		else {
			assert(std::get<0>(record[0]) == table_name);
		}
	}
};