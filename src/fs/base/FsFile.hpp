#pragma once

#include "FsObject.hpp"
#include "FsChunk.hpp"
#include "FsDirectory.hpp"

namespace supercloud {

    // TODO: currently, the file size is stored as a "cached value of the size of all current chunks".
    //          Is it interresting to store the size of each current chunk?
	class FsFile : public FsObject {
	protected:

        void replaceChunk(FsID old_chunk_id, FsID new_chunk_id, DateTime time) {
            bool found = false;
            {
                //std::lock_guard lock{ m_modify_load_mutex };
                auto it = std::find(this->m_current_state.begin(), this->m_current_state.end(), old_chunk_id);
                if (it != this->m_current_state.end()) {
                    found = true;
                    this->m_commits.emplace_back();
                    this->m_commits.back().id = new_chunk_id;
                    this->m_commits.back().date = time;
                    *it = new_chunk_id;
                    this->m_commits.back().changes.push_back({ old_chunk_id, new_chunk_id });
                }
            }
        }

        // in bytes, cache, but useful one
        size_t m_size;
	public:
        FsFile(FsID id, DateTime date, std::string name, CUGA puga, FsID parent) :FsObject(id, date, name, puga, parent) { assert(FsElt::isFile(id)); }
        FsFile(FsID id, DateTime date, std::string name, CUGA puga, FsID parent, FsID renamed_from) :FsObject(id, date, name, puga, parent, renamed_from) { assert(FsElt::isFile(id)); }

		virtual std::tuple<FsID, DateTime> getLastModification() const override {
			//std::lock_guard lock{ m_modify_load_mutex };
            if (m_commits.empty()) {
                return {m_id, this->m_creation_date};
            }
			return { m_commits.back().id, m_commits.back().date };
		}

        virtual size_t size() const override { return m_size; }
//
//		/**
//		 * Create a new chunk. It will not be added to the file content right now.
//		 * @param id if>0, it will use this id. If <=0, it will create a new id.
//		 * @return the newly added chunk.
//		 */
//		FsChunk createNewChunk(long id);
//
//		/**
//		 * Tell to the file that their new chunks order/content is this one now.
//		 * @param newList the new list of chunks to use.
//		 */
//		void setChunks(List<FsChunk> newList);
//
//		/**
//		 * Get the size of this file in io.
//		 * @return
//		 */
//		long getSize();
//
//
//		/**
//		 * read from buff.pos to buff.limit (so we are going to read limit-position bytes).
//		 * <br>note: you must ensure there are enough ytes to read before calling this method.
//		 * @param buff output buffer
//		 * @param offset from where we read on this file.
//		 */
//		public static void read(FsFile file, ByteBuff buff, long offset) {
//			//			Logs.logDb.info("READ FILE : "+file.getPath()+" read by "+offset+" -> "+(offset+buff.limit()-buff.position()));
//			//			Logs.logDb.info("now pos = "+buff.position());
//						//go to first chunk
//			long currentPos = 0;
//			int chunkIdx = 0;
//			FsChunk chunk = file.getChunks().get(chunkIdx);
//			while (currentPos + chunk.currentSize() <= offset && chunkIdx + 1 < file.getChunks().size()) {
//				currentPos += chunk.currentSize();
//				chunkIdx++;
//				chunk = file.getChunks().get(chunkIdx);
//			}
//			//choose first chunk
//			int chunkOffset = (int)(offset - currentPos);
//			//first read
////			Logs.logDb.info("read first chunk ("+chunkIdx+"�) chunk.currentSize()="+chunk.currentSize()+", chunkOffset="+chunkOffset+", buff.limit()="+buff.limit()+", buff.position()="+buff.position());
//			if (chunk.currentSize() - chunkOffset >= buff.limit() - buff.position()) {
//				//read some part
////				Logs.logDb.info("read inside : "+(buff.limit()-buff.position()));
//				chunk.read(buff, chunkOffset, buff.limit() - buff.position());
//			} else {
//				//full read
////				Logs.logDb.info("read a part : "+chunk.currentSize()+" / "+(buff.limit()-buff.position()));
//				chunk.read(buff, chunkOffset, chunk.currentSize() - chunkOffset);
//			}
//			//			Logs.logDb.info("now pos = "+buff.position()+", limit="+buff.limit()+", chunk.currentSize()="+chunk.currentSize()+", chunkOffset="+chunkOffset);
//			chunkIdx++;
//			//other reads
//			while (buff.position() < buff.limit()) {
//				//				Logs.logDb.info("read chunk n�"+chunkIdx+", now i need "+(buff.limit()-buff.position())+"more");
//				chunk = file.getChunks().get(chunkIdx);
//				if (chunk.currentSize() >= buff.limit() - buff.position()) {
//					//read some part
//					chunk.read(buff, 0, buff.limit() - buff.position());
//				} else {
//					//full read
//					chunk.read(buff, 0, chunk.currentSize());
//				}
//				//				Logs.logDb.info("now pos = "+buff.position()+" /"+buff.array().length);
//				//				Logs.logDb.info("sample = "+Arrays.toString(Arrays.copyOfRange(buff.array(), Math.max(0, buff.position()-10),  buff.position())));
//				chunkIdx++;
//			}
//		}
//
//		public static long getSize(FsFile file) {
//			if (file.getChunks().size() == 0) {
//				return 0;
//			}
//			long size = 0;
//			for (FsChunk chunk : file.getChunks()) {
//				size += chunk.currentSize();
//			}
//			return size;
//		}
//
//		public static void write(FsFile file, ByteBuff buff, long offset) {
//			Logs.logDb.info("WRITE FILE : " + file.getPath() + " writen by " + offset + " -> " + (offset + buff.limit() - buff.position()));
//			//			Logs.logDb.info("now pos = "+buff.position());
//
//						//grow file if needed
//			if (file.getSize() < offset + buff.limit() - buff.position()) {
//				growFile(file, offset + buff.limit() - buff.position());
//			}
//
//			//go to first chunk
//			long currentPos = 0;
//			int chunkIdx = 0;
//			FsChunk chunk = file.getChunks().get(chunkIdx);
//			while (currentPos + chunk.currentSize() <= offset && chunkIdx + 1 < file.getChunks().size()) {
//				currentPos += chunk.currentSize();
//				chunkIdx++;
//				chunk = file.getChunks().get(chunkIdx);
//			}
//			int chunkOffset = (int)(offset - currentPos);
//			//first write
////			Logs.logDb.info("write first chunk chunk.currentSize()="+chunk.currentSize()+", chunkOffset="+chunkOffset+",  buff.limit()="+ buff.limit()+", buff.position()="+buff.position()+
////					", cs-co = "+(chunk.currentSize()-chunkOffset)+" ? <= ? bl-bp="+(buff.limit()-buff.position()));
//			if (chunk.currentSize() - chunkOffset >= buff.limit() - buff.position()) {
//				//				Logs.logDb.info("write inside : "+(buff.limit()-buff.position()));
//								//write some part
//				chunk.write(buff, chunkOffset, buff.limit() - buff.position());
//			} else {
//				//full write
////				Logs.logDb.info("write a part : "+buff.position()+" -> "+(buff.limit()-buff.position())+" / "+chunk.currentSize());
//				chunk.write(buff, chunkOffset, chunk.currentSize() - chunkOffset);
//			}
//			//			Logs.logDb.info("now pos = "+buff.position());
//			chunkIdx++;
//			//other writes
//			while (buff.position() < buff.limit()) {
//				//				Logs.logDb.info("write chunk n�"+chunkIdx+", now i need "+(buff.limit()-buff.position())+"more");
//				chunk = file.getChunks().get(chunkIdx);
//				if (chunk.currentSize() > buff.limit() - buff.position()) {
//					//write some part
////					Logs.logDb.info("partial write = "+(buff.limit()-buff.position()));
//					chunk.write(buff, 0, buff.limit() - buff.position());
//				} else {
//					//full write
////					Logs.logDb.info("full write = "+chunk.currentSize());
//					chunk.write(buff, 0, chunk.currentSize());
//				}
//				//				Logs.logDb.info("now pos = "+buff.position());
//				chunkIdx++;
//			}
//			file.changes();
//		}
//
//		//		public static FsChunk getChunk(FsFile file, int idx) {
//		//			return file.getChunks().get(idx);
//		//		}
//
//		public static FsChunk getChunk(FsFile file, long id) {
//			for (FsChunk ch : file.getChunks()) {
//				if (ch.getId() == id) {
//					return ch;
//				}
//			}
//			return null;
//		}
//
//		public static FsChunk getFromAllChunk(FsFile file, long id) {
//			for (FsChunk ch : file.getAllChunks()) {
//				if (ch.getId() == id) {
//					return ch;
//				}
//			}
//			return null;
//		}
//
//		public static void truncate(FsFile file, long size) {
//
//			Logs.logDb.info("truncate FILE : " + file.getSize() + " -> " + size);
//			List<FsChunk> newLst = new ArrayList<>();
//			//go to first chunk
//			long currentPos = 0;
//			int chunkIdx = 0;
//			if (file.getSize() <= size) {
//				growFile(file, size);
//			} else {
//				FsChunk chunk = file.getChunks().get(chunkIdx);
//				newLst.add(chunk);
//				while (currentPos + chunk.currentSize() < size && chunkIdx + 1 < file.getChunks().size()) {
//					currentPos += chunk.currentSize();
//					chunkIdx++;
//					chunk = file.getChunks().get(chunkIdx);
//					newLst.add(chunk);
//				}
//				//truncate the last chunk
//				chunk.setCurrentSize((int)(size - currentPos));
//				//remove all others chunks
//				file.setChunks(newLst);
//			}
//		}
//
//		public static int newMaxSizeChunk = 1024 * 4; //min 4kio
//
//		public static void growFile(FsFile file, long newSize) {
//			Logs.logDb.info("WRITE FILE : growFile " + newSize);
//			synchronized(file) { //TODO : think more about sync 
//				if (file.getChunks().size() == 0) {
//					//				Logs.logDb.info("WRITE FILE : createFirstChunk ");
//					FsChunk newChunk = file.createNewChunk(-1);
//					//				Logs.logDb.info("WRITE FILE : flush1 ");
//					newChunk.setMaxSize(newMaxSizeChunk);
//					newChunk.setCurrentSize(0);
//					//				Logs.logDb.info("WRITE FILE : flush2 ");
//					newChunk.flush();
//					//				Logs.logDb.info("WRITE FILE : setLst ");
//					List<FsChunk> lst = new ArrayList<>(file.getChunks());
//					lst.add(newChunk);
//					file.setChunks(lst);
//					//				Logs.logDb.info("WRITE FILE : firstChunk created ");
//				}
//				long needNewSize = newSize - file.getSize();
//				//			Logs.logDb.info("WRITE FILE : file size :  "+file.getSize()+" < "+newSize);
//							//grow last chunk
//				FsChunk lastChunk = file.getChunks().get(file.getChunks().size() - 1);
//				//			Logs.logDb.info("WRITE FILE : needNewSize :  "+needNewSize+" , last max sie :  "+lastChunk.getMaxSize()+" (current) "+lastChunk.currentSize());
//				if (lastChunk.getMaxSize() - lastChunk.currentSize() >= needNewSize) {
//					lastChunk.setCurrentSize((int)(lastChunk.currentSize() + needNewSize));
//					lastChunk.changes();
//					//				Logs.logDb.info("WRITE FILE : setFistChunk "+lastChunk.currentSize());
//					return;
//				}
//				List<FsChunk> lst = new ArrayList<>(file.getChunks());
//				//else
//				needNewSize -= (lastChunk.getMaxSize() - lastChunk.currentSize());
//				lastChunk.setCurrentSize(lastChunk.getMaxSize());
//				while (needNewSize > 0) {
//					//create new chunk (bigger than last one, as it seems too small)
//					int newMaxSizeChunk = Math.min(Math.max(1024 * 4, lastChunk.getMaxSize() * 2), 1073741824); //max 1gio per chunk
//					FsChunk newChunk = file.createNewChunk(-1);
//					newChunk.setCurrentSize((int)Math.min(needNewSize, newMaxSizeChunk));
//					newChunk.setMaxSize(newMaxSizeChunk);
//					//				Logs.logDb.info("WRITE FILE : lastChunk.getMaxSize() "+newChunk.getMaxSize());
//					lst.add(newChunk);
//					//grow it
//					needNewSize -= newChunk.currentSize();
//					lastChunk = newChunk;
//					//				Logs.logDb.info("WRITE FILE : lastChunk.getMaxSize() "+newChunk.getMaxSize()+" : lastChunk.currentSize() "+newChunk.currentSize());
//					//				Logs.logDb.info("WRITE FILE : now grow to "+file.getSize()+" : reste "+needNewSize);
//				}
//				file.setChunks(lst);
//
//				Logs.logDb.info("End of grow, now i have " + file.getChunks());
//			}
//		}

	};

    class FsFileStub : public FsFile {
        FsFileStub(FsID id, DateTime date, std::string name, CUGA puga, FsID parent) : FsFile(id, date, name, puga, parent) {}
        virtual std::tuple<FsID, DateTime> getLastModification() { return {}; }
        std::vector<FsObjectCommit>& commits() { return m_commits; }
        std::vector<FsID>& current() { return m_current_state; }
    };
}
