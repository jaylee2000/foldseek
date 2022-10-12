#include <cassert>
#include "DBReader.h"
#include "Util.h"
#include "CommandCaller.h"
#include "Debug.h"
#include "FileUtil.h"
#include "LocalParameters.h"
#include "PrefilteringIndexReader.h"
#include "structuresearch.sh.h"
#include "structureiterativesearch.sh.h"

void setStructureSearchWorkflowDefaults(LocalParameters *p) {
    p->kmerSize = 0;
    p->maskMode = 0;
    p->maskProb = 0.99995;
    p->sensitivity = 9.5;
    p->maxResListLen = 1000;
    p->gapOpen = 10;
    p->gapExtend = 1;
    p->alignmentMode = Parameters::ALIGNMENT_MODE_SCORE_COV_SEQID;
    p->removeTmpFiles = true;
}

void setStructureSearchMustPassAlong(LocalParameters *p) {
    p->PARAM_K.wasSet = true;
    p->PARAM_MASK_RESIDUES.wasSet = true;
    p->PARAM_MASK_PROBABILTY.wasSet = true;
    p->PARAM_NO_COMP_BIAS_CORR.wasSet = true;
    p->PARAM_S.wasSet = true;
    p->PARAM_GAP_OPEN.wasSet = true;
    p->PARAM_GAP_EXTEND.wasSet = true;
    p->PARAM_ALIGNMENT_MODE.wasSet = true;
    p->PARAM_REMOVE_TMP_FILES.wasSet = true;
}

int structuresearch(int argc, const char **argv, const Command &command) {
    LocalParameters &par = LocalParameters::getLocalInstance();

    setStructureSearchWorkflowDefaults(&par);
    par.parseParameters(argc, argv, command, true, Parameters::PARSE_VARIADIC, 0);
    setStructureSearchMustPassAlong(&par);

    std::string tmpDir = par.filenames.back();
    std::string hash = SSTR(par.hashParameter(command.databases, par.filenames, *command.params));
    if (par.reuseLatest) {
        hash = FileUtil::getHashFromSymLink(tmpDir + "/latest");
    }
    tmpDir = FileUtil::createTemporaryDirectory(tmpDir, hash);
    par.filenames.pop_back();

    CommandCaller cmd;
    cmd.addVariable("TMP_PATH", tmpDir.c_str());
    cmd.addVariable("RESULTS", par.filenames.back().c_str());
    par.filenames.pop_back();
    std::string target = par.filenames.back().c_str();
    cmd.addVariable("TARGET_PREFILTER", (target+"_ss").c_str());
    par.filenames.pop_back();
    std::string query = par.filenames.back().c_str();
    cmd.addVariable("QUERY_PREFILTER", (query+"_ss").c_str());

    const bool isIndex = PrefilteringIndexReader::searchForIndex(target).empty() == false;
    cmd.addVariable("INDEXEXT", isIndex ? ".idx" : NULL);
    par.compBiasCorrectionScale = 0.15;
    cmd.addVariable("PREFILTER_PAR", par.createParameterString(par.prefilter).c_str());
    par.compBiasCorrectionScale = 0.5;
    if(par.alignmentType == LocalParameters::ALIGNMENT_TYPE_3DI){
        cmd.addVariable("ALIGNMENT_ALGO", "align");
        cmd.addVariable("QUERY_ALIGNMENT", (query+"_ss").c_str());
        cmd.addVariable("TARGET_ALIGNMENT", (target+"_ss").c_str());
        cmd.addVariable("ALIGNMENT_PAR", par.createParameterString(par.align).c_str());
    }else if(par.alignmentType == LocalParameters::ALIGNMENT_TYPE_TMALIGN){
        cmd.addVariable("ALIGNMENT_ALGO", "tmalign");
        cmd.addVariable("QUERY_ALIGNMENT", query.c_str());
        cmd.addVariable("TARGET_ALIGNMENT", target.c_str());
        cmd.addVariable("ALIGNMENT_PAR", par.createParameterString(par.tmalign).c_str());
    }else if(par.alignmentType == LocalParameters::ALIGNMENT_TYPE_3DI_AA){
        cmd.addVariable("ALIGNMENT_ALGO", "structurealign");
        cmd.addVariable("QUERY_ALIGNMENT", query.c_str());
        cmd.addVariable("TARGET_ALIGNMENT", target.c_str());
        cmd.addVariable("ALIGNMENT_PAR", par.createParameterString(par.structurealign).c_str());
    }
    cmd.addVariable("REMOVE_TMP", par.removeTmpFiles ? "TRUE" : NULL);
    cmd.addVariable("RUNNER", par.runner.c_str());
    cmd.addVariable("VERBOSITY", par.createParameterString(par.onlyverbosity).c_str());
    if(par.numIterations > 1){
        double originalEval = par.evalThr;
        par.evalThr = (par.evalThr < par.evalProfile) ? par.evalThr  : par.evalProfile;
        for (int i = 0; i < par.numIterations; i++) {
            if (i == (par.numIterations - 1)) {
                par.evalThr = originalEval;
            }
            par.addBacktrace = true;
            par.compBiasCorrectionScale = 0.15;
            cmd.addVariable(std::string("PREFILTER_PAR_" + SSTR(i)).c_str(),
                            par.createParameterString(par.prefilter).c_str());
            par.compBiasCorrectionScale = 0.5;
            if(par.alignmentType == LocalParameters::ALIGNMENT_TYPE_3DI){
                cmd.addVariable(std::string("ALIGNMENT_PAR_" + SSTR(i)).c_str(), par.createParameterString(par.align).c_str());
            }else if(par.alignmentType == LocalParameters::ALIGNMENT_TYPE_TMALIGN){
                cmd.addVariable(std::string("ALIGNMENT_PAR_" + SSTR(i)).c_str(), par.createParameterString(par.tmalign).c_str());
            }else if(par.alignmentType == LocalParameters::ALIGNMENT_TYPE_3DI_AA){
                cmd.addVariable(std::string("ALIGNMENT_PAR_" + SSTR(i)).c_str(), par.createParameterString(par.structurealign).c_str());
            }
        }

        cmd.addVariable("NUM_IT", SSTR(par.numIterations).c_str());
        par.scoringMatrixFile =  MultiParam<NuclAA<std::string>>(NuclAA<std::string>("blosum62.out", "nucleotide.out"));
        cmd.addVariable("PROFILE_PAR", par.createParameterString(par.result2profile).c_str());
        par.pca = 1.4;
        par.pcb = 1.5;
        par.scoringMatrixFile = "3di.out";
        par.seedScoringMatrixFile = "3di.out";
        par.maskProfile = 0;
        par.compBiasCorrection = 0;
        if(par.PARAM_E_PROFILE.wasSet == false){
            par.evalProfile = 0.1;
            par.evalThr = 0.1;
        }
        cmd.addVariable("PROFILE_SS_PAR", par.createParameterString(par.result2profile).c_str());
        cmd.addVariable("NUM_IT", SSTR(par.numIterations).c_str());
        cmd.addVariable("SUBSTRACT_PAR", par.createParameterString(par.subtractdbs).c_str());
        cmd.addVariable("VERBOSITY_PAR", par.createParameterString(par.onlyverbosity).c_str());


        std::string program = tmpDir + "/structureiterativesearch.sh";
        FileUtil::writeFile(program, structureiterativesearch_sh, structureiterativesearch_sh_len);
        cmd.execProgram(program.c_str(), par.filenames);
    }else{
        std::string program = tmpDir + "/structuresearch.sh";
        FileUtil::writeFile(program, structuresearch_sh, structuresearch_sh_len);
        cmd.execProgram(program.c_str(), par.filenames);
    }

    // Should never get here
    assert(false);
    // Should never get here
    return EXIT_FAILURE;
}
