
#include "FuseInterface.hpp"
#include "synch/FsExternalInterface.hpp"
#include "fs/base/FsStorage.hpp"
#include "fs/base/FsObject.hpp"
#include "fs/base/FsFile.hpp"

namespace supercloud {
	
/// <summary>
/// This class is the implementation interface for hte different fuse systems.
/// All fuse (winfsp, libfuse and osxfuse) implementation call this generic enry point,
///  and they all take care of translating the request & result to their platform interface.
/// </summary>
class FuseInterface {
protected:
	std::shared_ptr<FsExternalInterface> m_file_manager;
public:
	
	void init(const std::string& drivepath) {
		//TODO: should be done in the os impl, not here, as the drive path is platform-dependant.
		// switch (Platform.getNativePlatform().getOS()) {
			// case WINDOWS:
				// path = (""+drivepath.charAt(0)).toUpperCase()+":\\";
				// break;
			// default:
				// path = drivepath;//"/tmp/mntm_"+driveletter;
		// }
		// mount(Paths.get(path), false, false);
	}
    //TODO: push that into fuse impl
	static inline CUGA modeToCUGA(mode_t mode, CUGA old_rights = CUGA_7777){
        //stat.h: S_IFDIR;
        CUGA cuga;
        cuga |= (old_rights & CUGA_CR);
        cuga |= (old_rights & CUGA_CX);
        cuga |= (old_rights & CUGA_CW);
        cuga |= (FileStat::STAT_IRUSR & mode) ? CUGA_UR : 0;
        cuga |= (FileStat::STAT_IXUSR & mode) ? (CUGA_UR|CUGA_UX) : 0;
        cuga |= (FileStat::STAT_IWUSR & mode) ? CUGA_UR : 0;
        cuga |= (FileStat::STAT_IRGRP & mode) ? CUGA_GR : 0;
        cuga |= (FileStat::STAT_IXGRP & mode) ? (CUGA_GR | CUGA_GX) : 0;
        cuga |= (FileStat::STAT_IWGRP & mode) ? CUGA_GW : 0;
        cuga |= (FileStat::STAT_IROTH & mode) ? CUGA_AR : 0;
        cuga |= (FileStat::STAT_IXOTH & mode) ? (CUGA_AR | CUGA_AX) : 0;
        cuga |= (FileStat::STAT_IWOTH & mode) ? CUGA_AW : 0;
		return cuga;
	}
    //TODO: push that into fuse impl
	static inline mode_t CUGAToMode(CUGA cuga, bool is_dir){
        mode_t mode = 0;
    	if(cuga & CUGA_UR) mode |= FileStat::STAT_IRUSR;
    	if((cuga & CUGA_UR) && (cuga & CUGA_UX)) mode |= FileStat::STAT_IXUSR;
    	if(cuga & CUGA_UW) mode |= FileStat::STAT_IWUSR;
    	if(cuga & CUGA_GR) mode |= FileStat::STAT_IRGRP;
    	if((cuga & CUGA_GR) && (cuga & CUGA_GX)) mode |= FileStat::STAT_IXGRP;
    	if(cuga & CUGA_GW) mode |= FileStat::STAT_IWGRP;
    	if(cuga & CUGA_AR) mode |= FileStat::STAT_IROTH;
    	if((cuga & CUGA_AR) && (cuga & CUGA_AX)) mode |= FileStat::STAT_IXOTH;
    	if(cuga & CUGA_AW) mode |= FileStat::STAT_IWOTH;
    	if(is_dir){ mode |= FileStat::STAT_IFDIR; }else{ mode |= FileStat::STAT_IFREG; }
		return mode;
	}

    ino_t fsidTOino_t(FsID big_id) {
        //64b -> 16b  (/4)
        uint16_t output = uint16_t(big_id);
        output ^= (uint16_t(big_id >> 16));
        output ^= (uint16_t(big_id >> 32));
        output ^= (uint16_t(big_id >> 48));
        return output;
    }

    int16_t fillAttr(FsObjectPtr obj, Stat& stat_to_fill) {
        stat_to_fill.st_mode = CUGAToMode(obj->getCUGA(), FsElt::isDirectory(obj->getId()));
        stat_to_fill.st_ino = fsidTOino_t(obj->getId()); //uin16_t
        stat_to_fill.st_uid = m_file_manager->getUserId(obj->getOwner());
        stat_to_fill.st_gid = obj->getGroupId();
        stat_to_fill.st_ctime = obj->getDate();
        //if(stat_to_fill.st_birthtime != nullptr){
        //       stat_to_fill.st_birthtime.tv_sec = obj.getDate()/1000;
        //       stat_to_fill.st_birthtime.tv_nsec = obj.getDate()*1000;
        //}
        if (FsElt::isFile(obj->getId())) {
            stat_to_fill.st_size = FsElt::toFile(obj)->size();
        }
    }

    int16_t getattr(const std::string& pathstr, Stat& stat_to_fill) {
    	try{
        	// Logs.logOs.fine("getattr for (path) : "+path);
//        	log(/*"*/"get attr for (path) : "+path);
            std::filesystem::path attr_path = pathstr;
            std::future<SynchroDb::ObjectRequestAnswer> answer = m_file_manager->get(attr_path);
            std::future_status result = answer.wait_for(std::chrono::milliseconds(2000));
            if (result != std::future_status::ready) {
                return -EBUSY;
            }
            if (answer.get().error_code) {
                return -answer.get().error_code;
            }
    	    FsObjectPtr obj = answer.get().object;
    	    if(!obj){
    		    //Logs.logOs.warning("getattr : warn: can't find "+path);
    		    return -ENOENT;
    	    }
            fillAttr(obj, stat_to_fill);
    	}catch(std::exception e){
    		return -ENOENT; //ENOENT = Error NO ENTry
    	}
        return 0;
    }

    int16_t readlink(const std::string& path, char* buf, size_t size) {
    	//if(path.indexOf(0)>0) path = path.substring(0,path.indexOf(0));
    	log(std::string(" ")+path);
        return -EPERM;
    }
    
    
    int16_t mknod(const std::string& path, mode_t mode, dev_t rdev) {
    	log(std::string("mknod ")+path);
        return create(path, mode, FuseFileInfo{});
    }

    int16_t contains(FsDirPtr dir, const std::string& name) {
        std::future<SynchroDb::ObjectRequestAnswer> answer = m_file_manager->contains(dir, name);
        std::future_status result = answer.wait_for(std::chrono::milliseconds(2000));
        if (result != std::future_status::ready) {
            return EBUSY;
        }
        if (answer.get().error_code) {
            return answer.get().error_code;
        }
        return answer.get().object.operator bool()?0:ENOENT;
    }

    int16_t get(FsObjectPtr& obj, const std::filesystem::path& path) {
        std::future<SynchroDb::ObjectRequestAnswer> answer = m_file_manager->get(path);
        std::future_status result = answer.wait_for(std::chrono::milliseconds(2000));
        if (result != std::future_status::ready) {
            return EBUSY;
        }
        if (answer.get().error_code) {
            return answer.get().error_code;
        }
        obj = answer.get().object;
        return 0;
    }
    
    int16_t create(const std::string& path, mode_t mode, FuseFileInfo& fi) {
    	log(std::string("create for (path) : ")+path);
    	try{
            std::filesystem::path create_path = path;
            //get parent directory
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, create_path.parent_path()); error_code) {
                return -error_code;
            }
            if (!FsElt::isDirectory(obj->getId())) {
                return -ENOTDIR;
            }
            FsDirPtr parent_dir = FsElt::toDirectory(obj);
            //check it doesn't exist
            if (int16_t error_code = contains(parent_dir, create_path.filename().string()); error_code) {
                return -error_code;
            }
            //ask to create a file
            // file with this computerid, this user id, our default groupid, at this time.
            if (int error_code = m_file_manager->createFile(parent_dir, create_path.filename().string(), modeToCUGA(mode)); error_code) {
                return -error_code;
            }
            
            return 0;
    	}catch(std::exception e){
    		return -EIO;
    	}
    }

    
    int16_t mkdir(const std::string& path, mode_t mode) {
    	log(std::string("mkdir ")+path);
    	try{
            std::filesystem::path create_path = path;
            //get parent directory
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, create_path.parent_path()); error_code) {
                return -error_code;
            }
            if (!FsElt::isDirectory(obj->getId())) {
                return -ENOTDIR;
            }
            FsDirPtr parent_dir = FsElt::toDirectory(obj);
            //check it doesn't exist
            if (int error_code = contains(parent_dir, create_path.filename().string()); error_code) {
                return -error_code;
            }
            //ask to create a file
            // file with this computerid, this user id, our default groupid, at this time.
            if (int error_code = m_file_manager->createDirectory(parent_dir, create_path.filename().string(), modeToCUGA(mode)); error_code) {
                return -error_code;
            }
            return 0;
		}catch(std::exception e){
			return -EIO;
		}
    }
    
    int16_t unlink(const std::string& path) {
    	log(std::string(" ")+path);
    	try{
            std::filesystem::path delete_path = path;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, delete_path); error_code) {
                return -error_code;
            }
            // remove the object
            if (int error_code = m_file_manager->removeObject(obj); error_code) {
                return -error_code;
            }
        	
	        return 0;
		}catch(std::exception e){
			return -EIO;
		}
    }
    
    int16_t rmdir(const std::string& path) {
        log(std::string(" ") + path);
        try {
            std::filesystem::path delete_path = path;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, delete_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isDirectory(obj->getId())) {
                return -ENOTDIR;
            }
            // remove the object
            if (int error_code = m_file_manager->removeObject(obj); error_code) {
                return -error_code;
            }

            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
    }
    
    
    int16_t symlink(const std::string& oldpath, const std::string& newpath) {
        return -EPERM;
    }
    
    
    int16_t rename(const std::string& oldpath, const std::string& newpath) {
    	log(std::string(" ")+oldpath+" => "+newpath);
		try{

            std::filesystem::path old_path = oldpath;
            std::filesystem::path new_path = newpath;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, old_path); error_code) {
                return -error_code;
            }
            // move the object
            if (int error_code = m_file_manager->moveObject(obj, newpath); error_code) {
                return -error_code;
            }
	        return 0;
		
		}catch(std::exception e){
			return -EIO;
		}
    }
    
    
    int16_t link(const std::string& oldpath, const std::string& newpath) {
        return -EPERM; // ENOTSUPP?
    }
    
    int16_t chmod(const std::string& path, mode_t mode) {
    	try{
            std::filesystem::path obj_path = path;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, obj_path); error_code) {
                return -error_code;
            }
            if (int error_code = m_file_manager->setCUGA(obj, modeToCUGA(mode, obj->getCUGA())); error_code) {
                return -error_code;
            }
	        return 0;
		}catch(std::exception e){
			return -EIO;
		}
    }
    
    int16_t chown(const std::string& path_str, uid_t uid, gid_t gid) {
        try {
            uid_t our_id = m_file_manager->getUserId(m_file_manager->getOurComputerId());
            if (our_id == NO_COMPUTER_ID) {
                return -ENODEV; //no such device (network not initialized)
            }else if (uid != our_id) {
                return -EACCES; //Permission denied (you can only chown for yourself)
            }

            std::filesystem::path obj_path = path_str;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, obj_path); error_code) {
                return -error_code;
            }
            if (int error_code = m_file_manager->moveObject(obj, obj_path); error_code) {
                return -error_code;
            }
            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
    }
    
    int16_t truncate(const std::string& path_str, size_t size) {
    	try{
            std::filesystem::path trunc_path = path_str;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, trunc_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isFile(obj->getId())) {
                return -ENOENT;
            }
            FsFilePtr file = FsElt::toFile(obj);
            // resize the object
            std::future<SynchroDb::ObjectRequestAnswer> answer = m_file_manager->resize(file, size);
            std::future_status result = answer.wait_for(std::chrono::milliseconds(2000));
            if (result != std::future_status::ready) {
                return EBUSY;
            }
            if (answer.get().error_code) {
                return answer.get().error_code;
            }
	    	return 0;
    	}catch(std::exception e){
    		return -EIO;
    	}
    }
    
    int16_t open(const std::string& path_str, FuseFileInfo* fi) {
        try {
            std::filesystem::path trunc_path = path_str;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, trunc_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isFile(obj->getId())) {
                return -ENOENT;
            }
            FsFilePtr file = FsElt::toFile(obj);
            //return the file id
            fi->fh = file->getId();
            //something else to do????
            //TODO use flags? (like write-lock?)
            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
    }
    
    int16_t read(const std::string& path_str, uint8_t* buf, size_t size, size_t offset, FuseFileInfo* fi) {
        try {
            std::filesystem::path trunc_path = path_str;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, trunc_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isFile(obj->getId())) {
                return -ENOENT;
            }
            FsFilePtr file = FsElt::toFile(obj);
            if (offset + size > file->size()) {
                if (offset >= file->size()) {
                    return -ENODATA;
                } else {
                    size = file->size() - offset;
                }
            }
            //get the data
            std::future<SynchroDb::RequestAnswer> answer = m_file_manager->getData(file, buf, offset, size);
            std::future_status result = answer.wait_for(std::chrono::milliseconds(10000));
            if (result != std::future_status::ready) {
                return EBUSY;
            }
            if (answer.get().error_code) {
                return answer.get().error_code;
            }
            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
    }
    
    int16_t write(const std::string& path_str, uint8_t* buf, size_t size, size_t offset, FuseFileInfo* fi) {
        try {
            std::filesystem::path trunc_path = path_str;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, trunc_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isFile(obj->getId())) {
                return -ENOENT;
            }
            FsFilePtr file = FsElt::toFile(obj);
            if (offset + size > file->size()) {
                if (offset >= file->size()) {
                    return -ENODATA;
                } else {
                    size = file->size() - offset;
                }
            }
            //get the data
            std::future<SynchroDb::RequestAnswer> answer = m_file_manager->writeData(file, buf, offset, size);
            std::future_status result = answer.wait_for(std::chrono::milliseconds(10000));
            if (result != std::future_status::ready) {
                return EBUSY;
            }
            if (answer.get().error_code) {
                return answer.get().error_code;
            }
            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
    }
    
    
    int16_t statfs(const std::string& path, Statvfs* stbuf) {
        //TODO
        //if (Platform.getNativePlatform().getOS() == jnr.ffi.Platform.OS.WINDOWS) {
            // statfs needs to be implemented on Windows in order to allow for copying
            // data from other devices because winfsp calculates the volume size based
            // on the statvfs call.
            // see https://github.com/billziss-gh/winfsp/blob/14e6b402fe3360fdebcc78868de8df27622b565f/src/dll/fuse/fuse_intf.c#L654
            //if (path == "/") {
                stbuf->f_blocks = 1024 * 1024; // total data blocks in file system
                stbuf->f_frsize = 1024;        // fs block size
                stbuf->f_bfree = 1024 * 1024;  // free blocks in fs
            //}else{
            //	Logs.logOs.warning("statfs KO");
            //}
        //}
        return 0;
    }
    
    
    
    int16_t flush(const std::string& path, FuseFileInfo* fi) {
        //TODO: write not commit until flush
        return 0;
    }
    
    
    int16_t release(const std::string& path, FuseFileInfo* fi) {
        //TODO: for release an open lock?
        return 0;
    }
    
    
    int16_t fsync(const std::string& path, int isdatasync, FuseFileInfo* fi) {
        //? like  big flush?
        return 0;
    }
    
    
    int16_t setxattr(const std::string& path, const std::string& name, uint8_t* value, size_t size, int flags) {
    	log(std::string(" ")+path);
        return -ENOSYS;
    }
    
    
    int16_t getxattr(const std::string& path, const std::string& name, uint8_t* value, size_t size) {
    	log(std::string(" ")+path);
        return -ENOSYS;
    }
    
    
    int16_t listxattr(const std::string& path, uint8_t* list, size_t size) {
    	log(std::string(" ")+path);
        return -ENOSYS;
    }
    
    
    int16_t removexattr(const std::string& path, const std::string& name) {
    	log(std::string(" ")+path);
        return -ENOSYS;
    }
    
    
    int16_t opendir(const std::string& path_str, FuseFileInfo* fi) {
        try {
            std::filesystem::path open_path = path_str;
            //get object
            FsObjectPtr obj;
            if (int16_t error_code = get(obj, open_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isDirectory(obj->getId())) {
                return -ENOTDIR;
            }
            FsDirPtr dir = FsElt::toDirectory(obj);
            //return the dir id
            fi->fh = dir->getId();
            //something else to do????
            //TODO use flags? (like write-lock?)
            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
    }
    
    int16_t readdir(const std::string& path_str, void* buf, fuse_fill_dir_t filter, size_t offset, FuseFileInfo* fi) {
        try {
            std::filesystem::path dir_path = path_str;
            //get object
            FsObjectPtr obj_dir;
            if (int16_t error_code = get(obj_dir, dir_path); error_code) {
                return -error_code;
            }
            if (!FsElt::isDirectory(obj_dir->getId())) {
                return -ENOTDIR;
            }
            FsDirPtr dir = FsElt::toDirectory(obj_dir);
            std::future<SynchroDb::MultiObjectRequestAnswer> answer = m_file_manager->getAll(dir->getCurrent());
            std::future_status result = answer.wait_for(std::chrono::milliseconds(10000));
            if (result != std::future_status::ready) {
                return EBUSY;
            }
            if (answer.get().error_code) {
                return answer.get().error_code;
            }
            {
                Stat stat;
                fillAttr(dir, stat);
                filter(buf, ".", &stat, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
                filter(buf, "..", nullptr, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
            }
            //write all objs;
            for (FsObjectPtr& obj : answer.get().objects) {
                Stat stat;
                fillAttr(obj, stat);
                filter(buf, obj->getName().c_str(), &stat, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
            }
            return 0;
        }
        catch (std::exception e) {
            return -EIO;
        }
        return 0;
    }
    
    
    int16_t releasedir(const std::string& path, FuseFileInfo* fi) {
        return 0;
    }
    
    
    int16_t fsyncdir(const std::string& path, FuseFileInfo* fi) {
        return 0;
    }
    
    
    int16_t access(const std::string& path_str, int mask) {
        std::filesystem::path dir_path = path_str;
        //get object
        FsObjectPtr obj_dir;
        if (int16_t error_code = get(obj_dir, dir_path); error_code) {
            return -error_code;
        }
        //TODO: check rights
        return 0;
    }
    
    
    int16_t ftruncate(const std::string& path, size_t size, FuseFileInfo* fi) {
        // Avec  ftruncate(), le fichier doit être ouvert en écriture ; avec truncate(), il doit être accessible en écriture.
        return truncate(path, size);
    }
    
    
    int16_t fgetattr(const std::string& path, Stat& stbuf, FuseFileInfo* fi) {
        return getattr(path, stbuf);
    }
    
    
//    int16_t lock(const std::string& path, FuseFileInfo* fi, int cmd, Flock flock) {
////        return -ErrorCodes::ENOSYS;
//    	return 0;
//    }
    
    //
    //int16_t utimens(const std::string& path, Timespec[] timespec) {
    //    return -ErrorCodes::ENOSYS;
    //}
    //
    //
    //int16_t bmap(const std::string& path, @size_t long blocksize, long idx) {
    //    return 0;
    //}
    //
    //
    //int16_t ioctl(const std::string& path, int cmd, Pointer arg, FuseFileInfo* fi, @u_int32_t long flags, Pointer data) {
    //    return -ErrorCodes::ENOSYS;
    //}
    ////TODO
    //
    //
    //int16_t poll(const std::string& path, FuseFileInfo* fi, FusePollhandle ph, Pointer reventsp) {
    //    return -ErrorCodes::ENOSYS;
    //}
    
    
    //int16_t write_buf(const std::string& path, FuseBufvec buf, @off_t long off, FuseFileInfo* fi) {
    //	if(path.indexOf(0)>0) path = path.substring(0,path.indexOf(0));
    //	log(std::string(" ")+path);
    //	// TODO.
    //    // Some problem in implementation, but it not enabling by default
    //    int res;
    //    int size = (int) libFuse.fuse_buf_size(buf);
    //    FuseBuf flatbuf;
    //    FuseBufvec tmp = new FuseBufvec(Runtime.getSystemRuntime());
    //    long adr = MemoryIO.getInstance().allocateMemory(Struct.size(tmp), false);
    //    tmp.useMemory(Pointer.wrap(Runtime.getSystemRuntime(), adr));
    //    FuseBufvec.init(tmp, size);
    //    long mem = 0;
    //    if (buf.count.get() == 1 && buf.buf.flags.get() == FuseBufFlags.FUSE_BUF_IS_FD) {
    //        flatbuf = buf.buf;
    //    } else {
    //        res = -ErrorCodes::ENOMEM;
    //        mem = MemoryIO.getInstance().allocateMemory(size, false);
    //        if (mem == 0) {
    //            MemoryIO.getInstance().freeMemory(adr);
    //            return res;
    //        }
    //        tmp.buf.mem.set(mem);
    //        res = (int) libFuse.fuse_buf_copy(tmp, buf, 0);
    //        if (res <= 0) {
    //            MemoryIO.getInstance().freeMemory(adr);
    //            MemoryIO.getInstance().freeMemory(mem);
    //            return res;
    //        }
    //        tmp.buf.size.set(res);
    //        flatbuf = tmp.buf;
    //    }
    //    res = write(path, flatbuf.mem.get(), flatbuf.size.get(), off, fi);
    //    if (mem != 0) {
    //        MemoryIO.getInstance().freeMemory(adr);
    //        MemoryIO.getInstance().freeMemory(mem);
    //    }
    //    return res;
    //}
    //
    //
    //int16_t read_buf(const std::string& path, Pointer bufp, @size_t long size, @off_t long off, FuseFileInfo* fi) {
    //	if(path.indexOf(0)>0) path = path.substring(0,path.indexOf(0));
    //	log(std::string(" ")+path);
    //    // should be implemented or null
    //    long vecmem = MemoryIO.getInstance().allocateMemory(Struct.size(new FuseBufvec(Runtime.getSystemRuntime())), false);
    //    if (vecmem == 0) {
    //        return -ErrorCodes::ENOMEM;
    //    }
    //    Pointer src = Pointer.wrap(Runtime.getSystemRuntime(), vecmem);
    //    long memAdr = MemoryIO.getInstance().allocateMemory(size, false);
    //    if (memAdr == 0) {
    //        MemoryIO.getInstance().freeMemory(vecmem);
    //        return -ErrorCodes::ENOMEM;
    //    }
    //    Pointer mem = Pointer.wrap(Runtime.getSystemRuntime(), memAdr);
    //    FuseBufvec buf = FuseBufvec.of(src);
    //    FuseBufvec.init(buf, size);
    //    buf.buf.mem.set(mem);
    //    bufp.putAddress(0, src.address());
    //    int res = read(path, mem, size, off, fi);
    //    if (res >= 0)
    //        buf.buf.size.set(res);
    //    return res;
    //}
    
    
    int16_t flock(const std::string& path, FuseFileInfo* fi, int op) {
    	log(std::string("flock ")+path);
    	return -ENOSYS;
    }
    
    
    int16_t fallocate(const std::string& path, int32_t mode, size_t off, size_t length, FuseFileInfo* fi) {
        //TODO
    	return ftruncate(path, length + off, fi);
    }

};

} //namespace
