/****
DIAMOND protein aligner
Copyright (C) 2013-2017 Benjamin Buchfink <buchfink@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
****/

#ifndef SEQUENCE_SET_H_
#define SEQUENCE_SET_H_

#include <string>
#include <algorithm>
#include <queue>
#include <thread>
#include "../basic/sequence.h"
#include "string_set.h"
#include "../basic/shape_config.h"
#include "../basic/seed_iterator.h"
#include "../util/ptr_vector.h"
#include "../basic/value.h"

struct Sequence_set : public String_set<Letter, sequence::DELIMITER, 1>
{

	Sequence_set()
	{ }
	
	void print_stats() const
	{
		verbose_stream << "Sequences = " << this->get_length() << ", letters = " << this->letters() << ", average length = " << this->avg_len() << std::endl;
	}

	sequence operator[](size_t i) const
	{
		return sequence(ptr(i), length(i));
	}

	std::pair<size_t, size_t> len_bounds(size_t min_len) const
	{
		const size_t l(this->get_length());
		size_t max = 0, min = std::numeric_limits<size_t>::max();
		for (size_t i = 0; i < l; ++i) {
			max = std::max(this->length(i), max);
			min = this->length(i) >= min_len ? std::min(this->length(i), min) : min;
		}
		return std::pair<size_t, size_t>(min, max);
	}

	size_t max_len(size_t begin, size_t end) const
	{
		size_t max = 0;
		for (size_t i = begin; i < end; ++i)
			max = std::max(this->length(i), max);
		return max;
	}

	std::vector<size_t> partition(unsigned n_part) const
	{
		std::vector<size_t> v;
		const size_t l = (this->letters() + n_part - 1) / n_part;
		v.push_back(0);
		for (unsigned i = 0; i < this->get_length();) {
			size_t n = 0;
			while (i < this->get_length() && n < l)
				n += this->length(i++);
			v.push_back(i);
		}
		for (size_t i = v.size(); i < n_part + 1; ++i)
			v.push_back(this->get_length());
		return v;
	}

	size_t reverse_translated_len(size_t i) const
	{
		const size_t j(i - i % 6);
		const size_t l(this->length(j));
		if (this->length(j + 2) == l)
			return l * 3 + 2;
		else if (this->length(j + 1) == l)
			return l * 3 + 1;
		else
			return l * 3;
	}

	TranslatedSequence translated_seq(const sequence &source, size_t i) const
	{
		if (!align_mode.query_translated)
			return TranslatedSequence((*this)[i]);
		return TranslatedSequence(source, (*this)[i], (*this)[i + 1], (*this)[i + 2], (*this)[i + 3], (*this)[i + 4], (*this)[i + 5]);
	}

	size_t avg_len() const
	{
		return this->letters() / this->get_length();
	}

	template <typename _f, typename _filter>
	void enum_seeds(PtrVector<_f> &f, const std::vector<size_t> &p, size_t shape_begin, size_t shape_end, const _filter *filter, bool contig = false) const
	{
		std::vector<std::thread> threads;
		for (unsigned i = 0; i < f.size(); ++i)
			threads.emplace_back(enum_seeds_worker<_f, _filter>, &f[i], this, (unsigned)p[i], (unsigned)p[i + 1], std::make_pair(shape_begin, shape_end), filter, contig);
		for (auto &t : threads)
			t.join();
	}

	virtual ~Sequence_set()
	{ }

private:

	template<typename _f, typename _filter>
	void enum_seeds(_f *f, unsigned begin, unsigned end, std::pair<size_t, size_t> shape_range, const _filter *filter) const
	{
		vector<Letter> buf(max_len(begin, end));
		uint64_t key;
		for (unsigned i = begin; i < end; ++i) {
			const sequence seq = (*this)[i];
			Reduction::reduce_seq(seq, buf);
			for (size_t shape_id = shape_range.first; shape_id < shape_range.second; ++shape_id) {
				const Shape& sh = shapes[shape_id];
				if (seq.length() < sh.length_) continue;
				Seed_iterator it(buf, sh);
				size_t j = 0;
				while (it.good()) {
					if (it.get(key, sh))
						if (filter->contains(key, shape_id))
							(*f)(key, position(i, j), shape_id);
					++j;
				}
			}
		}
		f->finish();
	}

	template<typename _f, uint64_t _b, typename _filter>
	void enum_seeds_hashed(_f *f, unsigned begin, unsigned end, std::pair<size_t, size_t> shape_range, const _filter *filter) const
	{
		uint64_t key;
		for (unsigned i = begin; i < end; ++i) {
			const sequence seq = (*this)[i];
			for (size_t shape_id = shape_range.first; shape_id < shape_range.second; ++shape_id) {
				const Shape& sh = shapes[shape_id];
				if (seq.length() < sh.length_) continue;
				const uint64_t shape_mask = sh.long_mask();
				Hashed_seed_iterator<_b> it(seq, sh);
				size_t j = 0;
				while (it.good()) {
					if (it.get(key, shape_mask))
						if (filter->contains(key, shape_id))
							(*f)(key, position(i, j), shape_id);
					++j;
				}
			}
		}
		f->finish();
	}

	template<typename _f, typename _it, typename _filter>
	void enum_seeds_contiguous(_f *f, unsigned begin, unsigned end, const _filter *filter) const
	{
		uint64_t key;
		for (unsigned i = begin; i < end; ++i) {
			const sequence seq = (*this)[i];
			if (seq.length() < _it::length()) continue;
			_it it(seq);
			size_t j = 0;
			while (it.good()) {
				if (it.get(key))
					if (filter->contains(key, 0))
						if ((*f)(key, position(i, j), 0) == false)
							return;
				++j;
			}
		}
		f->finish();
	}

	template<typename _f, typename _filter>
	static void enum_seeds_worker(_f *f, const Sequence_set *seqs, unsigned begin, unsigned end, std::pair<size_t,size_t> shape_range, const _filter *filter, bool contig)
	{
		static const char *errmsg = "Unsupported contiguous seed.";
		if (shape_range.second - shape_range.first == 1 && shapes[shape_range.first].contiguous() && shapes.count() == 1 && (config.algo == Config::query_indexed || contig)) {
			const uint64_t b = Reduction::reduction.bit_size(), l = shapes[shape_range.first].length_;
			switch (l) {
			case 7:
				switch (b) {
				case 4:
					seqs->enum_seeds_contiguous<_f, Contiguous_seed_iterator<7, 4>,_filter>(f, begin, end, filter);
					break;
				default:
					throw std::runtime_error(errmsg);
				}
				break;
			case 6:
				switch (b) {
				case 4:
					seqs->enum_seeds_contiguous<_f, Contiguous_seed_iterator<6, 4>, _filter>(f, begin, end, filter);
					break;
				default:
					throw std::runtime_error(errmsg);
				}
				break;
			case 5:
				switch (b) {
				case 4:
					seqs->enum_seeds_contiguous<_f, Contiguous_seed_iterator<5, 4>, _filter>(f, begin, end, filter);
					break;
				default:
					throw std::runtime_error(errmsg);
				}
				break;
			default:
				throw std::runtime_error(errmsg);
			}
		}
		else if (config.hashed_seeds) {
			const uint64_t b = Reduction::reduction.bit_size();
			switch (b) {
			case 4:
				seqs->enum_seeds_hashed<_f, 4, _filter>(f, begin, end, shape_range, filter);
				break;
			default:
				throw std::runtime_error("Unsupported reduction.");
			}
		}
		else
			seqs->enum_seeds<_f,_filter>(f, begin, end, shape_range, filter);
	}

};

struct No_filter
{
	bool contains(uint64_t seed, uint64_t shape) const
	{
		return true;
	}
};

extern No_filter no_filter;

#endif /* SEQUENCE_SET_H_ */